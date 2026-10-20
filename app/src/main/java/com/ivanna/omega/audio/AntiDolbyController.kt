package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IVANNAApplication
import kotlin.math.max

/**
 * AntiDolbyController — UI/lifecycle adapter for the single shared CRNN path.
 *
 * The audio blocks are observed by CinematicEngineHost from AudioPipeline.
 * This class deliberately does not create a second TFLite interpreter or poll
 * an unfilled buffer. SAFMUS/native DSP remains the authoritative processing
 * path; model outputs are sanitized before crossing JNI.
 */
class AntiDolbyController(private val context: Context) {
    companion object {
        private const val TAG = "AntiDolbyController"
        private const val EMA_IN = 0.25f
        private const val EMA_OUT = 0.18f
    }

    var onDspUpdate: ((exciter: Float, width: Float, eqGainDb: Float) -> Unit)? = null

    @Volatile private var isInitialized = false
    @Volatile private var isAntiDolbyEnabled = false

    private var emaSpeech = 0f
    private var emaMusic = 0f
    private var emaBass = 0f
    private var smoothExciter = 0.32f
    private var smoothWidth = 0.50f
    private var smoothEq = 0f

    fun initialize() {
        if (isInitialized) return
        // The shared host owns the only CRNN interpreter. Initialization is lazy
        // until the user enables AntiDolby, avoiding duplicate model allocations.
        isInitialized = true
        Log.i(TAG, "AntiDolby listo; CRNN compartido con el pipeline de audio")
    }

    @Synchronized
    fun enableAntiDolby() {
        if (!isInitialized) initialize()
        if (isAntiDolbyEnabled) return

        CinematicEngineHost.onClassification = { speech, music, bass ->
            (context.applicationContext as? IVANNAApplication)
                ?.perceptualCortex
                ?.updateContentClassification(speech, music, bass)
            adjustParameters(speech, music, bass)
        }
        CinematicEngineHost.start(context.applicationContext, sampleRate = AudioPipeline.SAMPLE_RATE)
        isAntiDolbyEnabled = CinematicEngineHost.isActive
        Log.i(TAG, if (isAntiDolbyEnabled) "AntiDolby CRNN activo" else "AntiDolby no pudo arrancar")
    }

    @Synchronized
    fun disableAntiDolby() {
        if (!isAntiDolbyEnabled) return
        isAntiDolbyEnabled = false
        CinematicEngineHost.onClassification = null
        CinematicEngineHost.stop()
        (context.applicationContext as? IVANNAApplication)
            ?.perceptualCortex
            ?.clearContentClassification()
        emaSpeech = 0f
        emaMusic = 0f
        emaBass = 0f
        smoothExciter = 0.32f
        smoothWidth = 0.50f
        smoothEq = 0f
        onDspUpdate?.invoke(smoothExciter, smoothWidth, smoothEq)
        Log.i(TAG, "AntiDolby desactivado; parámetros restaurados")
    }

    /**
     * Compatibility entry point for legacy callers. The live pipeline now feeds
     * the shared CRNN directly; this method intentionally does not create a
     * second classifier or reinterpret already-downsampled buffers.
     */
    fun processAudioFrame(audioFrame: FloatArray) {
        if (audioFrame.isEmpty() || !isAntiDolbyEnabled) return
        // AudioPipeline/CinematicEngineHost is the sole live input to avoid
        // duplicated inference and competing writes to the native score bus.
    }

    private fun adjustParameters(speech: Float, music: Float, bass: Float) {
        val s = speech.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
        val m = music.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
        val b = bass.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f

        emaSpeech += EMA_IN * (s - emaSpeech)
        emaMusic += EMA_IN * (m - emaMusic)
        emaBass += EMA_IN * (b - emaBass)
        val silence = (1f - emaSpeech - emaMusic - emaBass).coerceIn(0f, 1f)

        val targetExciter = (emaSpeech * 0.12f + emaMusic * 0.68f + emaBass * 0.26f + silence * 0.32f).coerceIn(0f, 1f)
        val targetWidth = (emaSpeech * 0.22f + emaMusic * 0.78f + emaBass * 0.42f + silence * 0.50f).coerceIn(0f, 1f)
        val targetEq = (emaSpeech * -1.5f + emaMusic * 3.5f + emaBass * -3.5f).coerceIn(-6f, 6f)

        smoothExciter = (smoothExciter + EMA_OUT * (targetExciter - smoothExciter)).coerceIn(0f, 1f)
        smoothWidth = (smoothWidth + EMA_OUT * (targetWidth - smoothWidth)).coerceIn(0f, 1f)
        smoothEq = (smoothEq + EMA_OUT * (targetEq - smoothEq)).coerceIn(-6f, 6f)
        onDspUpdate?.invoke(smoothExciter, smoothWidth, smoothEq)
    }

    fun release() = disableAntiDolby()
}
