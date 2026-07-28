package com.ivanna.omega.audio

import android.media.AudioFormat
import android.media.AudioRecord
import android.util.Log
import com.ivanna.omega.core.NativeLibraryLoader
import kotlinx.coroutines.*

/**
 * AudioEngine v1.5 — Motor de audio DSP.
 */
class AudioEngine {
    companion object {
        private const val TAG = "AudioEngine"
        private const val SAMPLE_RATE = 96000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_STEREO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_FLOAT

        @Volatile private var libLoaded = NativeLibraryLoader.ensureLoaded()

        fun homeostasis(n: Float, omega: Float, mu: Float = 0.3f): Float {
            if (omega.isNaN() || omega.isInfinite()) return n
            if (n.isNaN() || n.isInfinite()) return omega
            return (n + mu * omega) / (1.0f + mu)
        }

        fun nativeSetAntiDolbyScoresStatic(speech: Float, music: Float, bass: Float) {
            if (!libLoaded) return
            try {
                nativeSetAntiDolbyScoresJni(speech, music, bass)
            } catch (e: UnsatisfiedLinkError) {
                Log.w(TAG, "nativeSetAntiDolbyScores JNI no disponible")
            }
        }

        @JvmStatic
        private external fun nativeSetAntiDolbyScoresJni(speech: Float, music: Float, bass: Float)

        fun nativeSetRouteProfileStatic(bassBoostDb: Float, dialogBoostDb: Float, widenerMult: Float) {
            if (!libLoaded) return
            val b = if (bassBoostDb.isFinite()) bassBoostDb.coerceIn(-18f, 18f) else 0f
            val d = if (dialogBoostDb.isFinite()) dialogBoostDb.coerceIn(-18f, 18f) else 0f
            val w = if (widenerMult.isFinite()) widenerMult.coerceIn(0f, 3f) else 1f
            runCatching { nativeSetRouteProfileJni(b, d, w) }
                .onFailure { Log.w(TAG, "nativeSetRouteProfileStatic: $it") }
        }

        @JvmStatic
        private external fun nativeSetRouteProfileJni(bassBoostDb: Float, dialogBoostDb: Float, widenerMult: Float)
    }

    private var audioRecord: AudioRecord? = null
    private var processingJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    private var exciterAmount = 0.3f
    private var eqGainAmount = 0.0f
    private var widthAmount = 0.5f

    fun initialize(sampleRate: Int = SAMPLE_RATE) {
        if (!libLoaded) {
            Log.w(TAG, "Librería nativa no disponible — modo degradado")
            return
        }
        nativeInit(sampleRate)
        Log.i(TAG, "AudioEngine inicializado @ $sampleRate Hz")
    }

    fun setExciter(amount: Float) {
        exciterAmount = amount.coerceIn(0f, 1f)
        if (libLoaded) nativeSetExciter(exciterAmount)
    }

    fun setGain(gain: Float) {
        if (!libLoaded) return
        val safe = if (gain.isFinite()) gain.coerceIn(-24f, 24f) else 0f
        runCatching { nativeSetGain(safe) }
            .onFailure { Log.e(TAG, "setGain crash-guard: $it") }
    }

    fun setMasterGain(gain: Float) {
        if (!libLoaded) return
        val safe = if (gain.isFinite()) gain.coerceIn(-24f, 24f) else 0f
        runCatching { nativeSetGain(safe) }
            .onFailure { Log.e(TAG, "setMasterGain crash-guard: $it") }
    }

    fun setBypass(bypass: Boolean) {
        if (libLoaded) nativeSetBypass(bypass)
    }

    fun setEqGain(gain: Float) {
        eqGainAmount = gain.coerceIn(-12f, 12f)
        if (libLoaded) nativeSetEqGain(eqGainAmount)
    }

    fun setWidth(width: Float) {
        widthAmount = width.coerceIn(0f, 1f)
        if (libLoaded) nativeSetWidth(widthAmount)
    }

    fun release() {
        processingJob?.cancel()
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        scope.cancel()
    }

    private external fun nativeInit(sampleRate: Int)
    private external fun nativeSetGain(gain: Float)
    private external fun nativeSetExciter(amount: Float)
    private external fun nativeSetEqGain(gain: Float)
    private external fun nativeSetWidth(width: Float)
    private external fun nativeSetBypass(bypass: Boolean)
    private external fun nativeSetRouteProfile(bassBoostDb: Float, dialogBoostDb: Float, widenerMult: Float)
    private external fun nativeSetManifoldEnabled(enabled: Boolean)
    private external fun nativeProcessAudio(
        inArray: FloatArray,
        outArray: FloatArray,
        frames: Int,
        channels: Int
    )
    private external fun nativeGetLufs(): Float
    private external fun nativeGetPeakDbfs(): Float
    private external fun nativeSetAntiDolbyScores(speech: Float, music: Float, bass: Float)

    // ── Benchmark logger (benchmark_logger.cpp) ───────────────────────────────
    // FASE 2: el brief de auditoría asumía nativeLogBenchmark(tag: String), pero
    // el símbolo JNI real (Java_com_ivanna_omega_audio_AudioEngine_nativeLogBenchmark)
    // toma 6 floats/int, no un tag. Firma tomada directo de benchmark_logger.cpp:77-90.
    private external fun nativeLogBenchmark(
        lufs: Float, peak: Float, speech: Float, music: Float, bass: Float, dolbyState: Int
    )
    private external fun nativeGetBenchmarkPath(): String

    fun logBenchmark(lufs: Float, peak: Float, speech: Float, music: Float, bass: Float, dolbyState: Int) {
        if (!libLoaded) return
        runCatching { nativeLogBenchmark(lufs, peak, speech, music, bass, dolbyState) }
            .onFailure { Log.w(TAG, "logBenchmark: $it") }
    }

    fun getBenchmarkPath(): String? {
        if (!libLoaded) return null
        return runCatching { nativeGetBenchmarkPath() }.getOrNull()
    }
}
