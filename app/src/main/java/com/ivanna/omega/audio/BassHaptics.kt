package com.ivanna.omega.audio

import android.content.Context
import android.os.Build
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.locks.LockSupport

/**
 * BassHaptics — "4D": el golpe de graves también se siente en el cuerpo, y llega JUNTO con el sonido.
 *
 * El hilo de audio (PlaybackCaptureService, Ruta A) llama a [onBlock] con el bloque ya procesado; el
 * [BassHapticAnalyzer] detecta golpes y, cada ~40 ms, la energía de sub sostenida (retumbo). Cada evento se
 * encola con su hora de ejecución = ahora + retardo de sincronía. Un hilo propio, fuera del hilo de audio, saca
 * los eventos vencidos y hace la llamada binder a Vibrator (puede bloquear); el hilo de audio nunca espera.
 *
 * Sincronía: con DAC USB-C o cable el sonido sale casi al instante, pero por Bluetooth llega 30–200 ms después
 * del bloque procesado mientras el teléfono vibra ya. Retardo aplicado = latencia de la ruta (la del códec BT que
 * ya estima [BluetoothAudioProfiler], 0 si es cable/USB) − latencia propia del motor háptico ([MOTOR_LATENCY_MS],
 * estimación) + ajuste fino del usuario. Los valores por códec son típicos, no medidos: el ajuste fino existe
 * para calibrar de oído.
 *
 * Límites honestos: solo actúa con el motor de la app (Ruta A); el motor de vibración está en el teléfono, no en
 * el equipo al que va el AUX; y depende de que el teléfono esté en contacto con el cuerpo o una superficie.
 */
object BassHaptics {
    private const val TAG = "BassHaptics"
    /** Latencia típica orden→vibración de un motor háptico de teléfono (ms). Estimación, no medida. */
    const val MOTOR_LATENCY_MS = 30
    private const val RING = 64
    private const val RUMBLE_EVERY_FRAMES = 1920   // 40 ms a 48 kHz

    private val analyzer = BassHapticAnalyzer(48_000)

    // Cola SPSC (hilo de audio → hilo de vibración). Entrada: (dueMs shl 9) or (kind shl 8) or amp
    private val ring = LongArray(RING)
    private val head = AtomicInteger(0)
    private val tail = AtomicInteger(0)

    @Volatile private var enabled = false
    @Volatile private var strength = 0.7f
    @Volatile private var rumble = 0f
    @Volatile private var trimMs = 0
    @Volatile private var autoLatencyMs = 0
    // Latencia REAL de la salida medida por RoomMeasurement (sonda de 1 kHz). Reemplaza la estimación por códec mientras
    // la ruta de salida siga siendo la misma con la que se midió. Incluye la latencia de entrada del micrófono, así que
    // es una cota superior: el ajuste fino sigue disponible.
    @Volatile private var measuredLatencyMs = -1
    @Volatile private var measuredRoute: OutputRoute? = null
    @Volatile private var currentRoute: OutputRoute = OutputRoute.UNKNOWN
    @Volatile private var routeLabel = "—"
    @Volatile private var vibrator: Vibrator? = null
    @Volatile private var worker: Thread? = null
    @Volatile private var resetRequested = false
    private var rumbleFrames = 0

    /** Último nivel de graves medido (0..1 aprox.) — para medidores en la UI. */
    val bassLevel: Float get() = analyzer.level

    /** Retardo efectivo (ms) que se aplica ahora entre el análisis y la vibración. */
    fun effectiveSyncMs(): Int {
        val m = measuredLatencyMs
        val base = if (m >= 0 && measuredRoute == currentRoute) m else autoLatencyMs
        return (base - MOTOR_LATENCY_MS + trimMs).coerceIn(0, 500)
    }

    /** Guarda la latencia real medida para la ruta de salida actual (se invalida sola si la ruta cambia). */
    fun setMeasuredLatency(context: Context, ms: Int) {
        val am = context.applicationContext.getSystemService(Context.AUDIO_SERVICE) as? android.media.AudioManager ?: return
        val route = runCatching { AudioRouteManager.detectOutputRoute(am) }.getOrDefault(OutputRoute.UNKNOWN)
        currentRoute = route
        measuredRoute = route
        measuredLatencyMs = ms.coerceIn(0, 500)
    }

    /** Texto para la UI: ruta de salida detectada y retardo aplicado. */
    fun describeSync(): String =
        "Salida: $routeLabel · retardo háptico ${effectiveSyncMs()} ms" + if (measuredLatencyMs >= 0 && measuredRoute == currentRoute) " (latencia medida)" else ""

    /** true si este teléfono tiene motor de vibración. */
    fun isSupported(context: Context): Boolean = resolve(context)?.hasVibrator() == true

    /** Aplica estado desde UI/arranque. Seguro llamarlo desde cualquier hilo. */
    @Synchronized
    fun configure(context: Context, enable: Boolean, strength01: Float, rumble01: Float = 0f, trimMs: Int = 0) {
        strength = strength01.coerceIn(0f, 1f)
        rumble = rumble01.coerceIn(0f, 1f)
        this.trimMs = trimMs.coerceIn(-100, 200)
        if (enable == enabled && (!enable || worker?.isAlive == true)) return
        if (enable) {
            val v = resolve(context)
            if (v == null || !v.hasVibrator()) {
                Log.w(TAG, "Sin motor de vibración: háptica de graves no disponible")
                enabled = false
                return
            }
            resetRequested = true
            enabled = true
            startWorker(context.applicationContext, v)
        } else {
            enabled = false
            tail.set(head.get())
            runCatching { vibrator?.cancel() }
            worker?.interrupt()
            worker = null
        }
    }

    /** Hilo de audio: sin asignaciones ni bloqueos. */
    fun onBlock(buffer: FloatArray, frames: Int) {
        if (!enabled) return
        if (resetRequested) { analyzer.reset(); rumbleFrames = 0; resetRequested = false }
        val dueMs = SystemClock.elapsedRealtime() + effectiveSyncMs()
        val amp = analyzer.process(buffer, frames, strength)
        if (amp > 0) { push(dueMs, 0, amp); rumbleFrames = 0; return }
        rumbleFrames += frames
        if (rumbleFrames >= RUMBLE_EVERY_FRAMES) {
            rumbleFrames = 0
            val r = BassHapticAnalyzer.rumbleAmplitude(analyzer.level, rumble)
            if (r >= BassHapticAnalyzer.MIN_AMPLITUDE) push(dueMs, 1, r)
        }
    }

    private fun push(dueMs: Long, kind: Int, amp: Int) {
        val h = head.get()
        if (h - tail.get() >= RING) return   // cola llena: se descarta, nunca se bloquea
        ring[h % RING] = (dueMs shl 9) or (kind.toLong() shl 8) or amp.toLong()
        head.set(h + 1)
    }

    private fun resolve(context: Context): Vibrator? {
        vibrator?.let { return it }
        val app = context.applicationContext
        val v = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            (app.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager)?.defaultVibrator
        else
            @Suppress("DEPRECATION") app.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        vibrator = v
        return v
    }

    /** Ruta de salida y latencia estimada (hilo de vibración; usa binder/AudioManager). */
    private fun refreshRoute(app: Context) {
        val am = app.getSystemService(Context.AUDIO_SERVICE) as? android.media.AudioManager ?: return
        val route = runCatching { AudioRouteManager.detectOutputRoute(am) }.getOrDefault(OutputRoute.UNKNOWN)
        currentRoute = route
        if (measuredRoute != null && measuredRoute != route) { measuredLatencyMs = -1; measuredRoute = null }
        when (route) {
            OutputRoute.BLUETOOTH -> {
                val p = BluetoothAudioProfiler.currentProfile()
                autoLatencyMs = p.latencyMs
                routeLabel = "Bluetooth ${p.codec.label}"
            }
            OutputRoute.USB -> { autoLatencyMs = 0; routeLabel = "USB-C DAC" }
            OutputRoute.WIRED_AUX -> { autoLatencyMs = 0; routeLabel = "AUX cableado" }
            OutputRoute.SPEAKER -> { autoLatencyMs = 0; routeLabel = "Bocina del teléfono" }
            OutputRoute.UNKNOWN -> { autoLatencyMs = 0; routeLabel = "desconocida" }
        }
    }

    private fun startWorker(app: Context, v: Vibrator) {
        if (worker?.isAlive == true) return
        val amplitudeControl = v.hasAmplitudeControl()
        worker = Thread({
            var nextRouteCheck = 0L
            while (enabled && !Thread.currentThread().isInterrupted) {
                val now = SystemClock.elapsedRealtime()
                if (now >= nextRouteCheck) { refreshRoute(app); nextRouteCheck = now + 2_000L }
                // Saca todos los eventos vencidos; vibra solo con el más reciente (los demás ya pasaron).
                var fire = -1L
                while (true) {
                    val t = tail.get()
                    if (t >= head.get()) break
                    val e = ring[t % RING]
                    if ((e shr 9) > now) break
                    tail.set(t + 1)
                    // Un golpe manda sobre un retumbo vencido del mismo instante
                    if (fire < 0 || ((e shr 8) and 1L) == 0L) fire = e
                }
                if (fire >= 0) {
                    val amp = (fire and 0xFF).toInt()
                    val isRumble = ((fire shr 8) and 1L) == 1L
                    val ms = if (isRumble) 45L else 25L + (25L * amp) / 255L
                    val effect = when {
                        amplitudeControl -> VibrationEffect.createOneShot(ms, amp.coerceIn(1, 255))
                        !isRumble && amp >= 120 -> VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE)
                        else -> null
                    }
                    if (effect != null) runCatching { v.vibrate(effect) }
                }
                LockSupport.parkNanos(3_000_000L)
            }
        }, "IvannaBassHaptics").also { it.isDaemon = true; it.start() }
    }
}
