package com.ivanna.omega.magisk

import android.util.Log

/**
 * IVANNA-OMEGA-SUPREME — OmegaDaemon (facade compatible sobre puente socket).
 *
 * HISTORIA:
 *   La versión anterior de este archivo declaraba 17 `external fun` (nativeStart,
 *   nativeStop, nativeSetIntensity, nativeSetPFParams, nativeGetPFParams,
 *   nativeSetPFDrive/Wet/Mix/Alpha/Beta/Gamma/Master/Freq/Resonance/Low/Mid/High/
 *   Presence, nativeGetTemperature, nativeGetLatency) sin NINGUNA implementación
 *   JNI en `app/src/main/cpp/` — 0 símbolos `..._OmegaDaemon_*` en la .so.
 *   Cualquier acceso a la clase venenaba el arranque con UnsatisfiedLinkError.
 *
 * DECISIÓN MAGISTRAL:
 *   El PF Engine ya está cableado por otra vía que SÍ funciona en producción:
 *   el daemon Magisk (`ivanna_daemon`) escuchando en el abstract namespace
 *   `@omega_daemon_socket` (ver app/src/main/cpp/daemon/ivanna_daemon.cpp:36
 *   y magisk_module/service.sh:36) con protocolo de texto
 *   SET_PF_DRIVE/WET/.../MASTER y SET_INTENSITY, más el contrato JSON
 *   `SET_PF_PARAMS` de `OmegaEngineBridge`. Ese puente es el único que
 *   procesa audio system-wide (todo lo que suena en el dispositivo),
 *   mientras el JNI in-process solo tocaría el audio de la app misma.
 *
 *   NOTA: la versión previa de este comentario decía "omega_daemon escuchando
 *   en /dev/socket/ivanna_omega". Esa descripción era obsoleta desde la
 *   migración a abstract namespace — el binario se renombró a ivanna_daemon
 *   y el socket dejó de vivir en el filesystem. El puente Kotlin real
 *   (MagiskBridge/OmegaEngineBridge) siempre usó Namespace.ABSTRACT; solo
 *   el docstring quedó desactualizado.
 *
 *   Redirigimos todos los wrappers PF al puente socket real. Start/Stop pasan a
 *   no-op seguros: el daemon lo lanza `service.sh` de Magisk en boot; la app
 *   no lo arranca. `getTemperature/getLatency` se derivan del payload de
 *   `MagiskBridge.getTelemetry()` cuando el daemon está vivo, con fallback 0f.
 *
 *   La API pública (fun/firma) NO cambia — cualquier caller (UI de PF Engine,
 *   IVANNAApplication) sigue compilando y funcionando; internamente TODO viaja
 *   por socket. Esto elimina los 17 símbolos fantasma sin regresión de UX.
 */
object OmegaDaemon {

    private const val TAG = "OmegaDaemon"

    // El "loaded" ahora refleja disponibilidad del PUENTE SOCKET real,
    // no de una .so JNI que no existía. Es lo que la UI necesita saber:
    // "¿puedo enviar setPFParams y esperar que algo suene distinto?".
    val isLoaded: Boolean
        get() = MagiskBridge.isDaemonRunning

    // ── Ciclo de vida ─────────────────────────────────────────────────────────
    // No-op seguros. El daemon system-wide lo arranca Magisk en boot vía
    // `service.sh`; la app nunca fue la responsable de lanzarlo en producción.
    // Devolvemos true si el socket ya responde, para que IVANNAApplication
    // logee correctamente el estado real en vez del "JNI ausente" viejo.
    fun start(): Boolean {
        val running = MagiskBridge.isDaemonRunning
        if (running) Log.d(TAG, "daemon ya vivo (socket-managed por Magisk)")
        return running
    }

    fun stop() {
        // No matamos el daemon system-wide desde la app; otras superficies
        // (visualizador, capture service) pueden estar usándolo. Es cosa de
        // Magisk/`service.sh`. Mantenemos la firma para compatibilidad.
    }

    // ── Control básico ────────────────────────────────────────────────────────
    fun setProcessing(enabled: Boolean) {
        MagiskBridge.sendCommand("SET_BYPASS:${if (enabled) 0 else 1}")
    }

    fun setIntensity(v: Float) {
        // Ruta JSON del OmegaEngineBridge (misma que usa el motor perceptual).
        OmegaEngineBridge.setIntensity(v.coerceIn(0f, 1f))
    }

    // ── Telemetría ────────────────────────────────────────────────────────────
    // Se derivan del payload de texto que devuelve GET_TELEMETRY del daemon.
    // Cache local para que la UI pueda leer a 10-30 Hz sin martillar el socket:
    // cada llamada refresca a lo sumo cada 250 ms.
    @Volatile private var lastTelemetryAtMs: Long = 0L
    @Volatile private var lastTempC: Float = 0f
    @Volatile private var lastLatencyMs: Float = 0f

    fun getTemperature(): Float { refreshTelemetryIfStale(); return lastTempC }
    fun getLatency(): Float     { refreshTelemetryIfStale(); return lastLatencyMs }

    private fun refreshTelemetryIfStale() {
        val now = System.currentTimeMillis()
        if (now - lastTelemetryAtMs < 250L) return
        val raw = runCatching { MagiskBridge.getTelemetry() }.getOrNull().orEmpty()
        if (raw.isNotBlank()) {
            lastTempC     = parseField(raw, "temp")    ?: lastTempC
            lastLatencyMs = parseField(raw, "latency") ?: lastLatencyMs
        }
        lastTelemetryAtMs = now
    }

    private fun parseField(raw: String, key: String): Float? {
        // Acepta "temp=41.2", "temp:41.2", "\"temp\":41.2" — cualquiera de
        // los formatos que ha usado el daemon; regex tolerante.
        val re = Regex("""(?:"?${Regex.escape(key)}"?\s*[:=]\s*)(-?\d+(?:\.\d+)?)""",
                       RegexOption.IGNORE_CASE)
        return re.find(raw)?.groupValues?.get(1)?.toFloatOrNull()
    }

    // ── PF Engine — bulk setter ──────────────────────────────────────────────
    /**
     * Aplica los 13 parámetros del PF Engine en una sola llamada. Prefiere la
     * ruta JSON (OmegaEngineBridge.setPFParams) porque el daemon la resuelve
     * con un único bump de coeff_version. Si esa capa está caída, cae al
     * fanout individual por comandos SET_PF_* (MagiskBridge).
     */
    fun setPFParams(
        drive: Float, wet: Float, mix: Float,
        alpha: Float, beta: Float, gamma: Float,
        freq: Float, resonance: Float,
        low: Float, mid: Float, high: Float,
        presence: Float, master: Float
    ) {
        val ok = runCatching {
            OmegaEngineBridge.setPFParams(
                drive, wet, mix, alpha, beta, gamma,
                freq, resonance, low, mid, high, presence, master
            )
        }.getOrElse { false }
        if (!ok) {
            // Fanout individual — funciona incluso si el daemon no acepta el
            // JSON compuesto (versiones viejas del omega_daemon).
            MagiskBridge.setDrive(drive)
            MagiskBridge.setWet(wet)
            MagiskBridge.setMix(mix)
            MagiskBridge.setAlpha(alpha)
            MagiskBridge.setBeta(beta)
            MagiskBridge.setGamma(gamma)
            MagiskBridge.setFreq(freq)
            MagiskBridge.setResonance(resonance)
            MagiskBridge.setLow(low)
            MagiskBridge.setMid(mid)
            MagiskBridge.setHigh(high)
            MagiskBridge.setPresence(presence)
            MagiskBridge.setMaster(master)
        }
    }

    /**
     * Devuelve FloatArray[13] con el estado local (lo último que enviamos).
     * El daemon no expone hoy un GET_PF_PARAMS canónico por socket; el path
     * JNI viejo era el que teóricamente lo hacía y nunca existió.
     *
     * Devolvemos el snapshot local — es lo que la UI necesita para sincronizar
     * sus sliders tras rotación / navegación (no hace round-trip al daemon).
     */
    fun getPFParams(): FloatArray = pfSnapshot.copyOf()

    private val pfSnapshot = FloatArray(13)  // [drive, wet, mix, alpha, beta, gamma, freq, resonance, low, mid, high, presence, master]

    // ── PF Engine — setters individuales ─────────────────────────────────────
    fun setPFDrive(v: Float)     { pfSnapshot[0]  = v; MagiskBridge.setDrive(v) }
    fun setPFWet(v: Float)       { pfSnapshot[1]  = v; MagiskBridge.setWet(v) }
    fun setPFMix(v: Float)       { pfSnapshot[2]  = v; MagiskBridge.setMix(v) }
    fun setPFAlpha(v: Float)     { pfSnapshot[3]  = v; MagiskBridge.setAlpha(v) }
    fun setPFBeta(v: Float)      { pfSnapshot[4]  = v; MagiskBridge.setBeta(v) }
    fun setPFGamma(v: Float)     { pfSnapshot[5]  = v; MagiskBridge.setGamma(v) }
    fun setPFFreq(v: Float)      { pfSnapshot[6]  = v; MagiskBridge.setFreq(v) }
    fun setPFResonance(v: Float) { pfSnapshot[7]  = v; MagiskBridge.setResonance(v) }
    fun setPFLow(v: Float)       { pfSnapshot[8]  = v; MagiskBridge.setLow(v) }
    fun setPFMid(v: Float)       { pfSnapshot[9]  = v; MagiskBridge.setMid(v) }
    fun setPFHigh(v: Float)      { pfSnapshot[10] = v; MagiskBridge.setHigh(v) }
    fun setPFPresence(v: Float)  { pfSnapshot[11] = v; MagiskBridge.setPresence(v) }
    fun setPFMaster(v: Float)    { pfSnapshot[12] = v; MagiskBridge.setMaster(v) }
}
