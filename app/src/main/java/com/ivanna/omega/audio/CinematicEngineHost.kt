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
 * hot-path real de audio, en AudioForegroundService) no tenía forma de
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

    /** Idempotente: si ya hay un engine corriendo, no lo recrea. */
    fun start(context: Context, sampleRate: Int) {
        if (engine == null) {
            engine = RealTimeCinematicEngine(
                AntiDolbyCrnnClassifier(context.applicationContext),
                sampleRate = sampleRate
            ).also { eng ->
                eng.onModeChanged = { mode, result ->
                    AudioEngine.nativeSetAntiDolbyScoresStatic(
                        result.speech, result.music, result.bass
                    )
                }
            }
            Log.i(TAG, "Engine creado @ ${sampleRate}Hz")
        }
        engine?.start()
        active = true
    }

    fun stop() {
        active = false
        engine?.stop()
    }

    /** Identidad si el engine no está activo — no altera el audio. */
    fun processBlock(buf: FloatArray): FloatArray {
        val eng = engine
        return if (active && eng != null) eng.processBlock(buf) else buf
    }
}
