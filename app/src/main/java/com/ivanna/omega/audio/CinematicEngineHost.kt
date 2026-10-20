package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.AntiDolbyCrnnClassifier

/**
 * CinematicEngineHost — dueño único del ciclo de vida de RealTimeCinematicEngine.
 *
 * PUNTO 2 (gap de conexión): RealTimeCinematicEngine.processBlock() clasificaba
 * con CRNN pero nadie lo llamaba desde el hilo de audio — AntiDolbyController
 * arrancaba el engine (enableAntiDolby → eng.start()) pero como owner era una
 * instancia local de un composable (DashboardScreen), y AudioPipeline (el
 * hot-path real de audio, en PlaybackCaptureService) no tenía forma de
 * alcanzarla. latestBuf nunca se actualizaba → el hilo clasificador dormía
 * cada 50ms sin clasificar nada real.
 *
 * Este singleton resuelve el mismatch de owners: AntiDolbyController controla
 * start()/stop() (ligado al toggle "Anti-Dolby adaptativo" de la UI) y
 * AudioPipeline llama processBlock() en cada bloque de audio real. Cuando
 * está inactivo, processBlock() es una identidad (no-op), preservando el
 * comportamiento previo del pipeline mientras el toggle esté apagado.
 */
object CinematicEngineHost {
    private const val TAG = "CinematicEngineHost"

    private var engine: RealTimeCinematicEngine? = null
    @Volatile private var active = false

    /** Single shared classification stream consumed by the UI controller. */
    @Volatile var onClassification: ((speech: Float, music: Float, bass: Float) -> Unit)? = null

    /** Idempotente: si ya hay un engine corriendo, no lo recrea. */
    @Synchronized
    fun start(context: Context, sampleRate: Int) {
        if (engine == null) {
            engine = RealTimeCinematicEngine(
                AntiDolbyCrnnClassifier(context.applicationContext),
                sampleRate = sampleRate
            ).also { eng ->
                eng.onModeChanged = { mode, result ->
                    // Sanitize model output before crossing the JNI/DSP boundary.
                    val speech = result.speech.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
                    val music = result.music.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
                    val bass = result.bass.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
                    AudioEngine.nativeSetAntiDolbyScoresStatic(speech, music, bass)
                    onClassification?.invoke(speech, music, bass)
                }
            }
            Log.i(TAG, "Engine creado @ ${sampleRate}Hz")
        }
        engine?.start()
        active = true
    }

    @Synchronized
    fun stop() {
        active = false
        engine?.stop()
    }

    val activeModeOrdinal: Int
        get() = if (active) (engine?.activeMode?.ordinal ?: 0) else 0

    /** True only while the AntiDolby classifier is actually running. */
    val isActive: Boolean
        get() = active && engine != null

    /**
     * AntiDolby must not inject cinematic reverb, delay or sub-harmonics.
     * Feed audio to the CRNN asynchronously and return the original buffer.
     */
    fun processBlock(buf: FloatArray, count: Int = buf.size, channels: Int = 2): FloatArray {
        val eng = engine
        if (active && eng != null) eng.observeBlock(buf, count, channels)
        return buf
    }
}
