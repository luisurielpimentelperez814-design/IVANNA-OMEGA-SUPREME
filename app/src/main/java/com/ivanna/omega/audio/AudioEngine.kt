package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import kotlinx.coroutines.*
import kotlin.math.*

/**
 * AudioEngine v1.5 — Motor de audio DSP.
 *
 * FIXES DE CONECTIVIDAD:
 *   1. nativeSetAntiDolbyScores() expuesta como companion method estático
 *      para que AudioPipeline pueda llamarla sin instancia.
 *   2. Se añade nativeSetAntiDolbyScores a las declaraciones external
 *      (faltaba — el orquestador C++ la implementa pero Kotlin no la declaraba
 *      en esta clase, solo en el JNI stub).
 *   3. companion init carga la librería UNA sola vez (idempotente).
 */
class AudioEngine {
    companion object {
        private const val TAG = "AudioEngine"
        private const val SAMPLE_RATE = 48000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_STEREO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_FLOAT

        @Volatile private var libLoaded = false

        init {
            if (!libLoaded) {
                try {
                    System.loadLibrary("ivanna_omega")
                    libLoaded = true
                    Log.i(TAG, "libivanna_omega cargada")
                } catch (e: UnsatisfiedLinkError) {
                    Log.e(TAG, "No se pudo cargar libivanna_omega: ${e.message}")
                }
            }
        }

        fun homeostasis(n: Float, omega: Float, mu: Float = 0.3f): Float {
            if (omega.isNaN() || omega.isInfinite()) return n
            if (n.isNaN() || n.isInfinite()) return omega
            return (n + mu * omega) / (1.0f + mu)
        }

        /**
         * FIX: método estático para que AudioPipeline envíe los scores
         * YAMNet al orquestador nativo sin necesitar una instancia de AudioEngine.
         */
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

        /**
         * FIX: método estático para que AudioRouteManager envíe el perfil de
         * compensación (BT/AUX/USB) al orquestador nativo.
         * Nota: se nombra con sufijo Static para no colisionar con el
         * external fun de instancia `nativeSetRouteProfile` (sin implementación
         * JNI, declarado más abajo, no usado directamente).
         */
        fun nativeSetRouteProfileStatic(bassBoostDb: Float, dialogBoostDb: Float, widenerMult: Float) {
            if (!libLoaded) return
            try {
                nativeSetRouteProfileJni(bassBoostDb, dialogBoostDb, widenerMult)
            } catch (e: UnsatisfiedLinkError) {
                Log.w(TAG, "nativeSetRouteProfileStatic JNI no disponible")
            }
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
        if (libLoaded) nativeSetGain(gain)
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

    // ── JNI natives ─────────────────────────────────────────────────────────────
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

    // FIX: declarada aquí también para instancias (el JNI la implementa)
    private external fun nativeSetAntiDolbyScores(speech: Float, music: Float, bass: Float)
}
