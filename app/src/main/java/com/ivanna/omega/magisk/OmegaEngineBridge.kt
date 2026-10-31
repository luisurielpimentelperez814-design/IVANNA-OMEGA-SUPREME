package com.ivanna.omega.magisk
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import org.json.JSONObject
import org.json.JSONArray
import com.ivanna.omega.safmus.SafmusUniversalKernel
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.atomic.AtomicBoolean

object OmegaEngineBridge {
    private const val TAG = "OmegaEngineBridge"
    private const val SOCKET_PRIMARY = "omega_daemon_socket"
    private const val MIN_PROTO_VERSION = 2   // coincide con OMEGA_PROTO_VERSION del daemon (command_server.h)
    private const val CONNECT_TIMEOUT = 2000
    @Volatile var isConnected = false; private set
    // Versión REAL del módulo reportada por el daemon en el handshake HELLO
    // (campo "moduleVersion"). Es la fuente viva: a diferencia de la prop
    // persist.ivanna.version (que queda obsoleta hasta el reinicio), ésta la
    // publica el binario que está corriendo AHORA. Vacía hasta el primer HELLO.
    @Volatile var liveModuleVersion: String = ""; private set
    @Volatile private var lastLatencyMs = 0f
    private val reconnecting = AtomicBoolean(false)

    // ── Keepalive: probe suave cada 5 s ──────────────────────────────────
    // Sin esto isConnected solo se refresca cuando la UI pulsa un boton —
    // tras un boot o una caida del daemon el panel quedaba DESCONECTADO
    // hasta intervencion manual. Un hilo daemon ligero hace probeSocket()
    // periodico (1 probe/5s, timeout 2s — coste despreciable) y la UI
    // siempre refleja el estado real del socket.
    @Volatile private var keepaliveStarted = false
    @Volatile private var lastSupremeAxesPayload: JSONObject? = null
    fun startKeepalive() {
        if (keepaliveStarted) return
        keepaliveStarted = true
        Thread({
            var alive = true
            var wasConnected = false
            while (alive) {
                try {
                    val nowConnected = connect()
                    if (nowConnected && !wasConnected) {
                        lastSupremeAxesPayload?.let { payload ->
                            runCatching { sendCommand(payload) }
                        }
                    }
                    wasConnected = nowConnected
                    Thread.sleep(5000)
                }
                catch (_: InterruptedException) { alive = false }  // return ilegal en lambda SAM → flag
                catch (_: Throwable) { /* probe ya maneja sus errores */ }
            }
        }, "omega-socket-keepalive").apply { isDaemon = true }.start()
    }
    // Canal de control autenticado: solo socket UNIX abstracto.
    // El daemon rechaza TCP por diseño; no anunciar un fallback que nunca puede
    // completar el contrato de seguridad.
    private interface Channel {
        val output: OutputStream
        val input: InputStream
        fun close()
    }
    private class UnixChannel(val s: LocalSocket) : Channel {
        override val output get() = s.outputStream
        override val input  get() = s.inputStream
        override fun close() = s.close()
    }
    @Volatile private var persistentChannel: Channel? = null

    fun connect(): Boolean {
        if (reconnecting.compareAndSet(false, true)) {
            try { return probeSocket() } finally { reconnecting.set(false) }
        }
        return isConnected
    }
    private fun probeSocket(): Boolean {
        // Un connect() exitoso solo prueba que el kernel aceptó el socket;
        // no prueba autorización SO_PEERCRED ni compatibilidad de protocolo.
        // Exigir respuesta HELLO real antes de anunciar CONECTADO.
        val ex = exchange(JSONObject().apply { put("action", "HELLO"); put("proto", MIN_PROTO_VERSION) })
        val reply = ex.reply?.let { runCatching { JSONObject(it) }.getOrNull() }
        val proto = reply?.optInt("proto", -1) ?: -1
        val ok = ex.reached && proto >= MIN_PROTO_VERSION
        if (ok) {
            val live = reply?.optString("moduleVersion", "") ?: ""
            if (live.isNotBlank()) liveModuleVersion = live
        } else {
            Log.w(TAG, "Probe HELLO no validado: reached=${ex.reached} proto=$proto; conexión rechazada o protocolo incompatible")
        }
        isConnected = ok
        return ok
    }
    @Synchronized
    private fun ensureSocket(): Channel? {
        if (persistentChannel != null && isConnected) return persistentChannel
        runCatching { persistentChannel?.close() }
        persistentChannel = null
        // Vía 1: Unix abstracto (rápida, sin overhead de red).
        val unix = runCatching {
            val sock = LocalSocket()
            sock.connect(LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT))
            sock.soTimeout = CONNECT_TIMEOUT
            UnixChannel(sock)
        }.getOrNull()
        if (unix != null) { persistentChannel = unix; isConnected = true; return unix }
        isConnected = false
        return null
    }
    @Synchronized
    /**
     * Handshake de protocolo: envía HELLO y exige que el daemon responda una
     * versión >= MIN_PROTO_VERSION. Si el daemon no responde o es más viejo,
     * la conexión se considera inválida (isConnected=false) aunque el socket
     * esté abierto — un socket vivo no implica contrato compatible.
     */
    fun handshake(): Boolean {
        return try {
            val resp = requestCommand(JSONObject().apply { put("action", "HELLO"); put("proto", MIN_PROTO_VERSION) })
            val proto = resp?.optInt("proto", -1) ?: -1
            val okProto = proto >= MIN_PROTO_VERSION
            if (!okProto) {
                Log.w(TAG, "Handshake HELLO: proto=$proto < mínimo $MIN_PROTO_VERSION — daemon incompatible")
                isConnected = false
            } else {
                // Capturar la versión viva del módulo si el daemon la publica.
                val live = resp?.optString("moduleVersion", "") ?: ""
                if (live.isNotBlank()) liveModuleVersion = live
                Log.i(TAG, "Handshake HELLO OK — proto=$proto daemon=${resp?.optString("daemon","?")} moduleVersion=$liveModuleVersion")
            }
            okProto
        } catch (e: Exception) {
            Log.w(TAG, "Handshake HELLO falló: ${e.message}")
            isConnected = false
            false
        }
    }

    private val ioExecutor = java.util.concurrent.Executors.newSingleThreadExecutor { r ->
        Thread(r, "omega-bridge-io").apply { isDaemon = true }
    }

    private val pendingByAction = java.util.concurrent.ConcurrentHashMap<String, JSONObject>()
    private val drainScheduled = java.util.concurrent.atomic.AtomicBoolean(false)

    private fun drainPending() {
        try {
            while (true) {
                val key = pendingByAction.keys.firstOrNull() ?: break
                val p = pendingByAction.remove(key) ?: continue
                sendCommandBlocking(p)
            }
        } finally {
            drainScheduled.set(false)
            // Carrera: un comando pudo llegar entre el último poll y el reset.
            if (pendingByAction.isNotEmpty() && drainScheduled.compareAndSet(false, true)) {
                ioExecutor.execute { drainPending() }
            }
        }
    }

    /**
     * FIX (estado del daemon parpadeando cada segundo, comandos perdidos): el daemon atiende UN
     * comando por conexión y la cierra (command_server.cpp: close(cfd) tras responder). La app
     * reutilizaba un «canal persistente»: el 1.er comando iba bien, el 2.º escribía en un socket ya
     * cerrado (EPIPE / read=-1) → isConnected=false y el comando se perdía; el siguiente reconectaba
     * → true. Resultado: «Corriendo · SHM lista» ↔ «Caído o sin root» y la mitad de los comandos
     * sin entregar. Ahora cada comando usa su propia conexión (barata en loopback) con un reintento.
     */
    @Synchronized
    private fun sendCommandBlocking(payload: JSONObject): Boolean {
        var ex = exchange(payload)
        if (!ex.reached) ex = exchange(payload)   // el daemon pudo reiniciarse entre sondas
        isConnected = ex.reached
        return ex.reached
    }

    private fun areFiniteNumericArgs(args: Array<out Any>): Boolean =
        args.all { value ->
            when (value) {
                is Float -> value.isFinite()
                is Double -> value.isFinite()
                else -> true
            }
        }

    /** SAFMUS boundary: no JSON command carrying NaN/Inf may cross into JNI or the daemon. */
    private fun hasFiniteNumericValues(value: Any?): Boolean {
        return when (value) {
            is Float -> value.isFinite()
            is Double -> value.isFinite()
            is JSONObject -> {
                val keys = value.keys()
                var valid = true
                while (keys.hasNext() && valid) {
                    valid = hasFiniteNumericValues(value.opt(keys.next()))
                }
                valid
            }
            is JSONArray -> {
                var valid = true
                for (i in 0 until value.length()) {
                    if (!hasFiniteNumericValues(value.opt(i))) { valid = false; break }
                }
                valid
            }
            is Iterable<*> -> value.all { hasFiniteNumericValues(it) }
            is Array<*> -> value.all { hasFiniteNumericValues(it) }
            else -> true
        }
    }

    fun sendCommand(payload: JSONObject): Boolean {
        if (!hasFiniteNumericValues(payload)) {
            Log.w(TAG, "SAFMUS rechazó comando con valor numérico no finito: ${payload.optString("action", "unknown")}")
            return false
        }
        val isMainThread = runCatching {
            android.os.Looper.getMainLooper()?.thread === Thread.currentThread()
        }.getOrDefault(false)
        if (isMainThread) {
            // Arrastrar un slider dispara decenas de comandos/s; con un ejecutor de
            // cola ilimitada cada uno esperaba su ida y vuelta al daemon (hasta 2 s
            // de timeout si el canal estaba stale) y la cola crecía sin tope: el
            // daemon recibía valores viejos tarde. Ahora el ÚLTIMO payload de cada
            // acción reemplaza al pendiente (estado, no eventos) y un solo drenaje
            // corre en el hilo de E/S.
            val key = payload.optString("action", "")
            pendingByAction[key] = payload
            if (drainScheduled.compareAndSet(false, true)) {
                ioExecutor.execute { drainPending() }
            }
            return isConnected
        }
        return sendCommandBlocking(payload)
    }
    @Synchronized
    fun requestCommand(payload: JSONObject): JSONObject? {
        if (!hasFiniteNumericValues(payload)) return null
        var ex = exchange(payload)
        if (!ex.reached) ex = exchange(payload)
        isConnected = ex.reached
        val text = ex.reply ?: return null
        return runCatching { JSONObject(text) }.getOrNull()
    }

    /**
     * Ruta B: pose de escena (azimut ±π, elevación ±π/4, distancia 0.5..2) hacia el efecto del sistema
     * (libomega_effect) vía bus de control v8. El daemon valida y rechaza lo fuera de rango.
     * Devuelve si el comando salió; la aplicación real se confirma con requestCommand (campo "ok").
     */
    fun setScenePose(azimuthRad: Float, elevationRad: Float, distance: Float): Boolean =
        sendCommand(JSONObject().apply {
            put("action", "SET_SCENE_POSE")
            put("azimuthRad", azimuthRad.toDouble())
            put("elevationRad", elevationRad.toDouble())
            put("distance", distance.toDouble())
        })

    fun clearScenePose(): Boolean =
        sendCommand(JSONObject().apply { put("action", "SET_SCENE_POSE"); put("clear", 1) })

    fun setPFParams(vararg params: Float): Boolean {
        if (params.any { !it.isFinite() }) return false
        return sendCommand(JSONObject().apply { put("action","SET_PF_PARAMS"); put("params", params.toList()) })
    }
    fun pushAdaptiveState(targetGain: Float, compAmount: Float, excRed: Float): Boolean {
        val g = SafmusUniversalKernel.sanitizeControlValue(targetGain, 0f, 2f) ?: return false
        val c = SafmusUniversalKernel.sanitizeControlValue(compAmount, 0f, 1f) ?: return false
        val e = SafmusUniversalKernel.sanitizeControlValue(excRed, 0f, 1f) ?: return false
        return sendCommand(JSONObject().apply { put("action","SET_ADAPTIVE_STATE"); put("targetGain",g); put("compAmount",c); put("excRed",e); put("timestamp",System.currentTimeMillis()) })
    }
    fun setRoom(rt60S: Float, wet: Float = 0.22f, roomIdx: Int = -1): Boolean {
        if (!rt60S.isFinite() || rt60S < 0f) return false
        val rt60 = if (rt60S == 0f) 0f
            else SafmusUniversalKernel.sanitizeControlValue(rt60S, 0.1f, 1.5f) ?: return false
        val safeWet = SafmusUniversalKernel.sanitizeControlValue(wet, 0f, 1f) ?: return false
        val safeIdx = roomIdx.coerceIn(-1, 199)
        val metadataCompatible = safeIdx == -1 || (
            isRirDatasetLoaded() && safeIdx < rirRoomCount() && rirRoomInfo(safeIdx) != null
        )
        // The native RIR loader resamples source WAVs to the active engine rate.
        val rirGate = SafmusUniversalKernel.gateRirConfiguration(
            explicitlySelectedOrMeasured = rt60 == 0f || rt60 in 0.1f..1.5f,
            metadataCompatible = metadataCompatible,
            sampleRateCompatible = safeIdx == -1 || metadataCompatible
        )
        if (!rirGate.allowed) {
            Log.w(TAG, "SAFMUS rechazó SET_ROOM_RT60: ${rirGate.reason}")
            return false
        }
        runCatching { nativeSetLocalRoom(rt60, safeWet, safeIdx) }
        return sendCommand(JSONObject().apply { put("action","SET_ROOM_RT60"); put("rt60",rt60); put("wet",safeWet); put("idx",safeIdx) })
    }
    fun disableRoom(): Boolean = setRoom(0f,0f)

    /** Sala RIR que el motor asigna para un RT60 objetivo (lectura del dataset cargado, no el comando). */
    data class RoomAssignment(
        val idx: Int, val name: String, val nominalRt60S: Float,
        val measuredRt60S: Float?, val volumeM3: Float, val dimsM: String
    ) {
        fun describe(): String = "sala #$idx $name · RT60 nominal %.2f s".format(nominalRt60S) +
            (measuredRt60S?.let { " (medido T20 %.2f s)".format(it) } ?: "") +
            " · %.0f m³ · %s".format(volumeM3, dimsM)
    }

    /**
     * Replica RirDataset::findNearestByRT60 (|rt60_nominal - objetivo| mínimo, primer empate) sobre
     * la tabla ya cargada en el motor; con índice explícito >= 0 devuelve esa sala.
     * null = dataset no cargado (la UI debe decir que NO hay sala asignada, no inventar una).
     */
    fun resolveRoomAssignment(targetRt60S: Float, explicitIdx: Int = -1): RoomAssignment? {
        if (!isRirDatasetLoaded()) return null
        val n = rirRoomCount()
        if (n <= 0) return null
        fun parse(i: Int): RoomAssignment? {
            val f = rirRoomInfo(i)?.split('|') ?: return null
            val nominal = f.getOrNull(1)?.toFloatOrNull() ?: return null
            return RoomAssignment(
                idx = i, name = f[0].removeSuffix(".wav"), nominalRt60S = nominal,
                measuredRt60S = f.getOrNull(5)?.toFloatOrNull()?.takeIf { it > 0f },
                volumeM3 = f.getOrNull(2)?.toFloatOrNull() ?: 0f, dimsM = f.getOrNull(4) ?: "?"
            )
        }
        if (explicitIdx in 0 until n) return parse(explicitIdx)
        if (!targetRt60S.isFinite() || targetRt60S <= 0.01f) return null
        var best = -1; var bestDiff = Float.MAX_VALUE
        for (i in 0 until n) {
            val nominal = rirRoomInfo(i)?.split('|')?.getOrNull(1)?.toFloatOrNull() ?: continue
            val d = kotlin.math.abs(nominal - targetRt60S)
            if (d < bestDiff) { bestDiff = d; best = i }
        }
        return if (best >= 0) parse(best) else null
    }
    fun getRoomStatus(): JSONObject? = requestCommand(JSONObject().apply { put("action","GET_ROOM_STATUS") })
    /**
     * FIX (botón TELEMETRY muerto): antes reutilizaba el canal persistente con
     * requestCommand(). Si ese canal se había quedado medio-abierto (daemon
     * reiniciado, conexión stale) el read() se bloqueaba hasta el timeout y el
     * botón parecía no hacer nada. Además un JSON de respuesta >4096 B o una
     * respuesta partida en 2 recv() dejaba el JSON truncado → JSONObject() lanzaba
     * y devolvía null → "Omega OFFLINE" aunque el daemon estuviera vivo.
     *
     * Ahora: canal EFÍMERO dedicado por consulta (sin estado stale), lectura en
     * bucle hasta '}' balanceado o EOF, timeout generoso, y fallback a PING si
     * GET_STATUS no parsea. Devuelve SIEMPRE una cadena legible con la causa.
     */
    data class ControlPlaneEvidence(
        val daemonReachable: Boolean,
        val snapshotGeneration: Long,
        val dspAppliedGeneration: Long,
        val processedFrames: Long,
        val effectState: Int,
        val status: String,
        val effectConsumerPresent: Boolean
    ) {
        val dspHasAppliedCurrentSnapshot: Boolean
            get() = snapshotGeneration > 0L && dspAppliedGeneration >= snapshotGeneration &&
                effectState == 2 && status == "dsp_block_applied"
    }

    /** Read-back real: el daemon informa la generación del último snapshot usado por un bloque DSP completo. */
    fun requestControlPlaneEvidence(): ControlPlaneEvidence? {
        val raw = queryDaemonEphemeral(JSONObject().apply { put("action", "GET_STATUS") }) ?: return null
        val json = runCatching { JSONObject(raw) }.getOrNull() ?: return null
        if (!json.optBoolean("ok", false)) return null
        return ControlPlaneEvidence(
            daemonReachable = true,
            snapshotGeneration = json.optLong("generation", 0L),
            dspAppliedGeneration = json.optLong("dsp_applied_generation", 0L),
            processedFrames = json.optLong("dsp_frames", 0L),
            effectState = json.optInt("effect_state", -1),
            status = json.optString("status", "unknown"),
            effectConsumerPresent = json.optString("consumer", "null") == "omega_effect"
        )
    }

    /**
     * Abre una conexión NUEVA (Unix abstracto, luego TCP fallback), envía un
     * comando JSON y lee la respuesta completa balanceando llaves. Sin canal
     * persistente → imposible que quede stale. Cierra siempre.
     */
    /** reached = se pudo abrir la conexión y entregar el comando; reply = respuesta JSON (si la hubo). */
    private class Exchange(val reached: Boolean, val reply: String?)

    private fun queryDaemonEphemeral(payload: JSONObject): String? = exchange(payload).reply

    @Synchronized
    private fun exchange(payload: JSONObject): Exchange {
        val t0 = System.nanoTime()
        val channel: Channel = runCatching {
            val sock = LocalSocket()
            sock.connect(LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT))
            sock.soTimeout = 3000
            UnixChannel(sock)
        }.getOrNull() ?: return Exchange(false, null)
        try {
            channel.output.write(payload.toString().toByteArray(Charsets.UTF_8))
            channel.output.flush()
            val buf = ByteArray(8192)
            val sb = StringBuilder()
            var depth = 0
            var started = false
            var inStr = false
            var esc = false
            while (true) {
                val n = try { channel.input.read(buf) } catch (_: Exception) { -1 }
                if (n <= 0) break
                for (i in 0 until n) {
                    val c = buf[i].toInt().toChar()
                    sb.append(c)
                    if (inStr) { if (esc) esc = false else if (c == '\\') esc = true else if (c == '"') inStr = false; continue }
                    when (c) {
                        '"' -> inStr = true
                        '{' -> { depth++; started = true }
                        '}' -> if (started && --depth == 0) {
                            lastLatencyMs = (System.nanoTime() - t0) / 1_000_000f
                            return Exchange(true, sb.toString())
                        }
                    }
                }
                if (sb.length > 65536) break
            }
            val raw = sb.toString().trim()
            lastLatencyMs = (System.nanoTime() - t0) / 1_000_000f
            return Exchange(true, if (raw.isNotEmpty()) raw else null)
        } catch (e: Exception) {
            Log.w(TAG, "exchange error: ${e.message}")
            return Exchange(false, null)
        } finally {
            runCatching { channel.close() }
        }
    }

    fun setIntensity(intensity: Float): Boolean {
        val safe = SafmusUniversalKernel.sanitizeControlValue(intensity, 0f, 1f) ?: return false
        return sendCommand(JSONObject().apply { put("action","SET_INTENSITY"); put("intensity", safe) })
    }
    fun setIntensity(intensity: Double): Boolean {
        if (!intensity.isFinite()) return false
        return setIntensity(intensity.toFloat())
    }
    fun setIntensity(vararg args: Any): Boolean {
        if (!areFiniteNumericArgs(args)) return false
        return sendCommand(JSONObject().apply { put("action","SET_INTENSITY"); put("args", args.map { it.toString() }) })
    }

    fun setHarmonicGain(gain: Float): Boolean {
        val safe = SafmusUniversalKernel.sanitizeControlValue(gain, 0f, 1f) ?: return false
        return sendCommand(JSONObject().apply { put("action","SET_HARMONIC_GAIN"); put("gain", safe) })
    }
    fun setHarmonicGain(gain: Double): Boolean {
        if (!gain.isFinite()) return false
        return setHarmonicGain(gain.toFloat())
    }

    fun pushSAFState(deltaEnergy: Float, metricNorm: Float, memory: Float, gain: Float): Boolean {
        if (!deltaEnergy.isFinite() || !metricNorm.isFinite() || !memory.isFinite() || !gain.isFinite()) return false
        return sendCommand(JSONObject().apply { put("action","PUSH_SAF_STATE"); put("deltaEnergy", deltaEnergy); put("metricNorm", metricNorm); put("memory", memory); put("gain", gain.coerceIn(0f, 1f)) })
    }
    /**
     * Envía el vector latente q[7] real de Φ_SAF-Room^∞ (SaFRoomBridge.getParams())
     * al daemon — este es el único punto donde q[7] llega al bus (saf_q en
     * OmegaDspSnapshot), que omega_effect.cpp aplica vía setSafLatentParams().
     * Antes de este método, SpatialAudioPanel activaba el switch SAF pero nunca
     * enviaba el vector calculado; el DSP nunca recibía la calibración real.
     */
    fun pushSafLatentQ(q: FloatArray, gain: Float = 1.0f): Boolean {
        if (q.size < 7 || q.take(7).any { !it.isFinite() }) return false
        val safeGain = SafmusUniversalKernel.sanitizeControlValue(gain, 0f, 1f) ?: return false
        val safeQ = FloatArray(7) { i -> q[i].coerceIn(-1f, 1f) }
        runCatching { nativePushLocalSafLatent(safeQ, safeGain) }
        return sendCommand(JSONObject().apply {
            put("action", "PUSH_SAF_STATE")
            put("q", JSONArray(safeQ.toList()))
            put("gain", safeGain)
        })
    }
    fun pushSAFState(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_SAF_STATE"); put("state", json) })
    fun pushSAFState(vararg args: Any): Boolean {
        if (!areFiniteNumericArgs(args)) return false
        return sendCommand(JSONObject().apply { put("action","PUSH_SAF_STATE"); put("args", args.map { it.toString() }) })
    }

    fun sendPerceptualState(compressor: Float, exciterRed: Float, highCut: Float, spatialWidth: Float, loudnessTarget: Float, harmonicGain: Float, antiDolby: Float): Boolean {
        val comp = SafmusUniversalKernel.sanitizeControlValue(compressor, -24f, 0f) ?: return false
        val exciter = SafmusUniversalKernel.sanitizeControlValue(exciterRed, 0f, 1f) ?: return false
        val cutoff = SafmusUniversalKernel.sanitizeControlValue(highCut, 1000f, 24000f) ?: return false
        val width = SafmusUniversalKernel.sanitizeControlValue(spatialWidth, 0f, 2f) ?: return false
        val loudness = SafmusUniversalKernel.sanitizeControlValue(loudnessTarget, -30f, -5f) ?: return false
        val harmonic = SafmusUniversalKernel.sanitizeControlValue(harmonicGain, 0f, 1f) ?: return false
        val anti = SafmusUniversalKernel.sanitizeControlValue(antiDolby, 0f, 1f) ?: return false
        return sendCommand(JSONObject().apply { put("action","SEND_PERCEPTUAL_STATE"); put("compressor", comp); put("exciterRed", exciter); put("highCut", cutoff); put("spatialWidth", width); put("loudnessTarget", loudness); put("harmonicGain", harmonic); put("antiDolby", anti) })
    }
    fun sendPerceptualState(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","SEND_PERCEPTUAL_STATE"); put("state", json) })
    fun sendPerceptualState(vararg args: Any): Boolean {
        if (!areFiniteNumericArgs(args)) return false
        return sendCommand(JSONObject().apply { put("action","SEND_PERCEPTUAL_STATE"); put("args", args.map { it.toString() }) })
    }

    fun setRouteProfile(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("profile", json) })
    fun setRouteProfile(a: Float, b: Float, c: Float): Boolean {
        if (!a.isFinite() || !b.isFinite() || !c.isFinite()) return false
        return sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("a", a); put("b", b); put("c", c) })
    }
    fun setRouteProfile(a: Float, b: Float): Boolean {
        if (!a.isFinite() || !b.isFinite()) return false
        return sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("a", a); put("b", b) })
    }
    fun setRouteProfile(vararg args: Any): Boolean {
        if (args.any { it is Float && !it.isFinite() || it is Double && !it.isFinite() }) return false
        return sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("args", args.map { it.toString() }) })
    }

    fun pushYamnetScores(speech: Float, music: Float, classId: Int, confidence: Float): Boolean {
        val s = SafmusUniversalKernel.sanitizeControlValue(speech, 0f, 1f) ?: return false
        val m = SafmusUniversalKernel.sanitizeControlValue(music, 0f, 1f) ?: return false
        val conf = SafmusUniversalKernel.sanitizeControlValue(confidence, 0f, 1f) ?: return false
        return sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("speech", s); put("music", m); put("classId", classId.coerceAtLeast(0)); put("confidence", conf) })
    }
    fun pushYamnetScores(speech: Float, music: Float, classId: Float, confidence: Float): Boolean =
        if (!classId.isFinite()) false else pushYamnetScores(speech, music, classId.toInt(), confidence)
    fun pushYamnetScores(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("scores", json) })
    fun pushYamnetScores(scores: FloatArray): Boolean {
        if (scores.any { !it.isFinite() }) return false
        return sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("scores", scores.map { it.coerceIn(0f, 1f) }) })
    }
    fun pushYamnetScores(scores: List<Float>): Boolean {
        if (scores.any { !it.isFinite() }) return false
        return sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("scores", scores.map { it.coerceIn(0f, 1f) }) })
    }
    fun pushYamnetScores(vararg args: Any): Boolean {
        if (args.any { it is Float && !it.isFinite() || it is Double && !it.isFinite() }) return false
        return sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("args", args.map { it.toString() }) })
    }

    fun setPinnaMetrics(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","SET_PINNA_METRICS"); put("metrics", json) })
    fun setPinnaMetrics(width: Float, height: Float, depth: Float): Boolean {
        val w = SafmusUniversalKernel.sanitizeControlValue(width, 20f, 45f) ?: return false
        val h = SafmusUniversalKernel.sanitizeControlValue(height, 50f, 80f) ?: return false
        val d = SafmusUniversalKernel.sanitizeControlValue(depth, 15f, 35f) ?: return false
        return sendCommand(JSONObject().apply { put("action","SET_PINNA_METRICS"); put("width", w); put("height", h); put("depth", d) })
    }
    fun setPinnaMetrics(vararg args: Any): Boolean {
        if (!areFiniteNumericArgs(args)) return false
        return sendCommand(JSONObject().apply { put("action","SET_PINNA_METRICS"); put("args", args.map { it.toString() }) })
    }

    /**
     * Sincroniza los 5 Ejes de Supremacía Cuántico-Neuromórfica con el daemon Magisk (`ivanna_daemon`),
     * el cual los publica mediante seqlock en `OmegaControlBus` (`OmegaDspSnapshot`) hacia `omega_effect.so`
     * en `audioserver` (Ruta B system-wide).
     */
    fun pushSupremeAxesState(
        warpedLatticeEnabled: Boolean,
        warpedLatticeMicroChirp: Boolean,
        warpedLatticeBlDrive: Float,
        warpedLatticeLambda: Float,
        transharmonicCvnnEnabled: Boolean,
        transharmonicHarmonicGain: Float,
        transharmonicImdCancel: Float,
        transharmonicAnalogTapeDrive: Float = 0.25f,
        snnHoaUpmixerEnabled: Boolean,
        snnHoaImmersivity: Float,
        snnSpikeThreshold: Float,
        pinnaManifoldEnabled: Boolean,
        pinnaManifoldWetMix: Float,
        pinnaConchaDepth: Float,
        pinnaHelixCurl: Float,
        pinnaHeadWidth: Float,
        farrowMsoEnabled: Boolean,
        msoItdNanoseconds: Float,
        ebpfBypassActive: Boolean
    ): Boolean {
        val blDrive = SafmusUniversalKernel.sanitizeControlValue(warpedLatticeBlDrive, 0f, 1f) ?: return false
        val lambda = SafmusUniversalKernel.sanitizeControlValue(warpedLatticeLambda, 0.50f, 0.85f) ?: return false
        val harmonic = SafmusUniversalKernel.sanitizeControlValue(transharmonicHarmonicGain, 0f, 1f) ?: return false
        val imd = SafmusUniversalKernel.sanitizeControlValue(transharmonicImdCancel, 0f, 1f) ?: return false
        val tape = SafmusUniversalKernel.sanitizeControlValue(transharmonicAnalogTapeDrive, 0f, 1f) ?: return false
        val immersivity = SafmusUniversalKernel.sanitizeControlValue(snnHoaImmersivity, 0f, 1f) ?: return false
        val spike = SafmusUniversalKernel.sanitizeControlValue(snnSpikeThreshold, 0.15f, 1.50f) ?: return false
        val wet = SafmusUniversalKernel.sanitizeControlValue(pinnaManifoldWetMix, 0f, 1f) ?: return false
        val concha = SafmusUniversalKernel.sanitizeControlValue(pinnaConchaDepth, -1f, 1f) ?: return false
        val helix = SafmusUniversalKernel.sanitizeControlValue(pinnaHelixCurl, -1f, 1f) ?: return false
        val headWidth = SafmusUniversalKernel.sanitizeControlValue(pinnaHeadWidth, -1f, 1f) ?: return false
        val itd = SafmusUniversalKernel.sanitizeControlValue(msoItdNanoseconds, -50000f, 50000f) ?: return false
        val payload = JSONObject().apply {
            put("action", "SET_SUPREME_AXES")
            put("warpedLatticeEnabled", if (warpedLatticeEnabled) 1.0f else 0.0f)
            put("warpedLatticeMicroChirp", if (warpedLatticeMicroChirp) 1.0f else 0.0f)
            put("warpedLatticeBlDrive", blDrive)
            put("warpedLatticeLambda", lambda)
            put("transharmonicCvnnEnabled", if (transharmonicCvnnEnabled) 1.0f else 0.0f)
            put("transharmonicHarmonicGain", harmonic)
            put("transharmonicImdCancel", imd)
            put("transharmonicAnalogTapeDrive", tape)
            put("snnHoaUpmixerEnabled", if (snnHoaUpmixerEnabled) 1.0f else 0.0f)
            put("snnHoaImmersivity", immersivity)
            put("snnSpikeThreshold", spike)
            put("pinnaManifoldEnabled", if (pinnaManifoldEnabled) 1.0f else 0.0f)
            put("pinnaManifoldWetMix", wet)
            put("pinnaConchaDepth", concha)
            put("pinnaHelixCurl", helix)
            put("pinnaHeadWidth", headWidth)
            put("farrowMsoEnabled", if (farrowMsoEnabled) 1.0f else 0.0f)
            put("msoItdNanoseconds", itd)
            put("ebpfBypassActive", if (ebpfBypassActive) 1.0f else 0.0f)
        }
        val accepted = sendCommand(payload)
        if (accepted) lastSupremeAxesPayload = payload
        return accepted
    }

    fun disconnect() { isConnected = false; runCatching { persistentChannel?.close() }; persistentChannel = null }
    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs

    // ── RIR dataset: listado real de salas + fallback sin root (2026-09-19) ──
    @JvmStatic external fun nativeGetRirRoomCount(): Int
    @JvmStatic external fun nativeGetLocalRirTailStatus(): Int
    @JvmStatic external fun nativeIsRirDatasetLoaded(): Boolean
    @JvmStatic external fun nativeGetRirRoomInfo(idx: Int): String?
    @JvmStatic external fun nativeSetRirDataDir(dir: String): Boolean
    @JvmStatic external fun nativeSetLocalRoom(rt60S: Float, wet: Float, roomIdx: Int)
    @JvmStatic external fun nativePushLocalSafLatent(q: FloatArray, gain: Float)

    /** Estado de la cola RIR local: -1 desconocido/no inicializada, 0 normal, 1 cola retrasada omitida por presupuesto. */
    @JvmStatic
    fun localRirTailStatus(): Int = runCatching { nativeGetLocalRirTailStatus() }.getOrDefault(-1)

    /** true si el dataset RIR (200 salas) esta cargado en el motor nativo. */
    @JvmStatic
    fun isRirDatasetLoaded(): Boolean = runCatching { nativeIsRirDatasetLoaded() }.getOrDefault(false)

    /** Numero de salas indexadas (0 si no hay dataset). */
    @JvmStatic
    fun rirRoomCount(): Int = runCatching { nativeGetRirRoomCount() }.getOrDefault(0)

    /** "nombre|rt60|volumen|distancia|WxHxD" de la sala idx, o null. */
    @JvmStatic
    fun rirRoomInfo(idx: Int): String? = runCatching { nativeGetRirRoomInfo(idx) }.getOrNull()

    /**
     * Fallback sin root: extrae assets/ivanna_omega/rir/ (metadata.csv +
     * rir_0000.wav..rir_0199.wav, ~2 MB) a filesDir/rir la primera vez y
     * apunta el dataset nativo ahi. Idempotente; no-op si root ya lo cargo.
     */
    @JvmStatic
    fun ensureRirDataset(ctx: android.content.Context): Boolean {
        if (isRirDatasetLoaded()) return true
        return runCatching {
            // Dataset COMPLETO (200 WAV + metadata, ~2 MB) en disco ANTES de indexar: el loader nativo descarta
            // filas sin WAV y marcaba "cargado" con 5 salas. Idempotente. Llamar fuera del hilo UI.
            val dst = com.ivanna.omega.core.IvannaAssetProvider.ensureRirComplete(ctx)
            nativeSetRirDataDir(dst.absolutePath)
        }.getOrDefault(false)
    }
}
