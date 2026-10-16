package com.ivanna.omega.audio

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.locks.LockSupport

/**
 * BassHaptics — "4D": el golpe de graves también se siente en el cuerpo.
 *
 * El hilo de audio (PlaybackCaptureService, Ruta A) llama a [onBlock] con el bloque ya procesado; el
 * [BassHapticAnalyzer] detecta golpes de graves y deja la amplitud en un AtomicInteger. Un hilo propio, fuera del
 * hilo de audio, hace la llamada binder a Vibrator (que puede bloquear) — el hilo de audio nunca espera.
 *
 * Límites honestos: solo actúa con el motor de la app (Ruta A); en Ruta B (módulo en audioserver) no hay buffer
 * en la app y no vibra. La latencia del motor háptico del teléfono (decenas de ms) no se puede compensar, y el
 * efecto depende de que el teléfono esté en contacto con el cuerpo o con una superficie.
 */
object BassHaptics {
    private const val TAG = "BassHaptics"
    private val analyzer = BassHapticAnalyzer(48_000)
    private val pending = AtomicInteger(0)

    @Volatile private var enabled = false
    @Volatile private var strength = 0.7f
    @Volatile private var rumble = 0f
    @Volatile private var vibrator: Vibrator? = null
    @Volatile private var worker: Thread? = null
    @Volatile private var resetRequested = false

    /** Último nivel de graves medido (0..1 aprox.) — para medidores en la UI. */
    val bassLevel: Float get() = analyzer.level

    /** true si este teléfono tiene motor de vibración. */
    fun isSupported(context: Context): Boolean = resolve(context)?.hasVibrator() == true

    /** Aplica estado desde UI/arranque. Seguro llamarlo desde cualquier hilo. */
    @Synchronized
    fun configure(context: Context, enable: Boolean, strength01: Float, rumble01: Float = 0f) {
        strength = strength01.coerceIn(0f, 1f)
        rumble = rumble01.coerceIn(0f, 1f)
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
            startWorker(v)
        } else {
            enabled = false
            pending.set(0)
            runCatching { vibrator?.cancel() }
            worker?.interrupt()
            worker = null
        }
    }

    /** Hilo de audio: sin asignaciones ni bloqueos. */
    fun onBlock(buffer: FloatArray, frames: Int) {
        if (!enabled) return
        if (resetRequested) { analyzer.reset(); resetRequested = false }
        val amp = analyzer.process(buffer, frames, strength)
        if (amp > 0) pending.set(amp)
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

    private fun startWorker(v: Vibrator) {
        if (worker?.isAlive == true) return
        val amplitudeControl = v.hasAmplitudeControl()
        var lastRumbleNs = 0L
        worker = Thread({
            while (enabled && !Thread.currentThread().isInterrupted) {
                val amp = pending.getAndSet(0)
                if (amp > 0) {
                    // 25..50 ms: un golpe seco, no un zumbido.
                    val ms = 25L + (25L * amp) / 255L
                    val effect = if (amplitudeControl) VibrationEffect.createOneShot(ms, amp)
                    else if (amp >= 120) VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE)
                    else null
                    if (effect != null) runCatching { v.vibrate(effect) }
                    lastRumbleNs = System.nanoTime()
                } else if (amplitudeControl) {
                    // Retumbo: sigue el sub sostenido entre golpes, cada ~40 ms (solo si el usuario lo subió)
                    val now = System.nanoTime()
                    if (now - lastRumbleNs >= 40_000_000L) {
                        val r = BassHapticAnalyzer.rumbleAmplitude(analyzer.level, rumble)
                        if (r >= BassHapticAnalyzer.MIN_AMPLITUDE) {
                            runCatching { v.vibrate(VibrationEffect.createOneShot(45L, r)) }
                            lastRumbleNs = now
                        }
                    }
                }
                LockSupport.parkNanos(4_000_000L)
            }
        }, "IvannaBassHaptics").also { it.isDaemon = true; it.start() }
    }
}
