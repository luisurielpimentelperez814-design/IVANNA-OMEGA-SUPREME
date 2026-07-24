package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.YamnetClassifier
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.core.ParameterStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * VoiceProtectionController — protege la inteligibilidad de la voz en el
 * camino REAL de audio (DSPBridge.nativeProcess, el mismo que usa
 * IvannaBridgePlayer).
 *
 * POR QUÉ ES UN CONTROLADOR NUEVO Y NO SE REUTILIZA AntiDolbyController:
 *   AntiDolbyController ya usa YamnetClassifier, pero su salida
 *   (nativeSetAntiDolbyScores) va a `audio_orchestrator.cpp`/AudioEngine —
 *   un pipeline confirmado huérfano (nativeProcessAudio() nunca se invoca
 *   con audio real, ver auditoría de esta sesión). Reutilizar ese camino
 *   habría heredado el mismo problema. Este controlador es independiente,
 *   con su propia instancia de YamnetClassifier, y escribe directo al
 *   camino real: DSPBridge.nativeSetVoiceProtectScore() → g_voice_protect_score
 *   → blend seco/procesado dentro de DSPBridge.nativeProcess (C++, ver
 *   ivanna_omega_jni.cpp). No se toca AntiDolbyController (regla de oro).
 *
 * FUNCIONAMIENTO:
 *   IvannaBridgePlayer alimenta cada bloque decodificado (estéreo, sample
 *   rate real del archivo) vía feed(). Acá se hace downmix a mono +
 *   resample simple (decimación/interpolación lineal) a 16kHz, se acumula
 *   hasta los 15600 samples que YAMNet necesita, se clasifica en una
 *   corrutina aparte (no bloquea el loop de decodificación/escritura de
 *   IvannaBridgePlayer), se suaviza con EMA, y se empuja el score de voz
 *   al nativo.
 */
class VoiceProtectionController(context: Context) {

    companion object {
        private const val TAG = "VoiceProtection"
        private const val TARGET_SR = 16000
        private const val YAMNET_INPUT_LENGTH = 15600
        private const val SPEECH_THRESHOLD = 0.15f  // YAMNet da logits bajos por clase; ya es score post-softmax-like
        private const val EMA_ALPHA = 0.25f

        // Perfiles independientes (fuerza fija cuando manualMode = true)
        //   podcast    — fuerza balanceada, ideal para locución con música de fondo
        //   call       — fuerza alta, prioriza inteligibilidad total
        //   broadcast  — fuerza media-alta, para radio/streaming
        //   whisper    — fuerza máxima, susurros y voz baja
        val PROFILE_SCORES: Map<String, Float> = mapOf(
            "podcast"   to 0.65f,
            "call"      to 0.85f,
            "broadcast" to 0.75f,
            "whisper"   to 1.00f
        )
    }

    private val classifier = YamnetClassifier(context)
    private val scope = CoroutineScope(Dispatchers.Default + Job())
    private var classifyJob: Job? = null
    private val store = ParameterStore(context.applicationContext)

    private var resampleAccumulator = 0.0
    private val monoBuffer = FloatArray(YAMNET_INPUT_LENGTH)
    private var bufferFill = 0
    private var smoothedScore = 0f

    @Volatile var enabled = true

    // FEATURE (perfiles independientes): cuando manualMode = true, se ignora YAMNet
    // y se empuja directamente PROFILE_SCORES[profile] al nativo. Último perfil
    // seleccionado se persiste en SharedPreferences (ParameterStore).
    @Volatile var manualMode: Boolean = store.isVoiceProtectionManual()
    @Volatile var profile: String = store.getVoiceProtectionProfile()
        set(value) {
            field = value
            store.setVoiceProtectionProfile(value)
            if (manualMode && enabled) {
                val s = PROFILE_SCORES[value] ?: 0.65f
                smoothedScore = s
                DSPBridge.setVoiceProtectScore(s)
            }
        }

    fun setManualMode(on: Boolean) {
        manualMode = on
        store.setVoiceProtectionManual(on)
        if (on && enabled) {
            val s = PROFILE_SCORES[profile] ?: 0.65f
            smoothedScore = s
            DSPBridge.setVoiceProtectScore(s)
        }
    }

    /** Recuperación automática: restaura el último perfil/estado. */
    fun restoreFromPrefs() {
        enabled = store.wasVoiceProtectionActive()
        manualMode = store.isVoiceProtectionManual()
        profile = store.getVoiceProtectionProfile()
        if (enabled && manualMode) {
            val s = PROFILE_SCORES[profile] ?: 0.65f
            smoothedScore = s
            DSPBridge.setVoiceProtectScore(s)
        } else if (!enabled) {
            DSPBridge.setVoiceProtectScore(0f)
        }
    }

    /**
     * Alimenta un bloque estéreo intercalado recién decodificado (mismo
     * formato que recibe DSPBridge.process()) para análisis de voz. No
     * modifica el audio; es solo para clasificación.
     */
    fun feed(stereoInterleaved: FloatArray, frames: Int, sourceSampleRate: Int) {
        if (!enabled) return
        if (frames <= 0 || sourceSampleRate <= 0) return

        // Modo manual: perfil fija el score, no se re-clasifica.
        if (manualMode) {
            val s = PROFILE_SCORES[profile] ?: 0.65f
            if (smoothedScore != s) {
                smoothedScore = s
                DSPBridge.setVoiceProtectScore(s)
            }
            return
        }

        // Downmix a mono + resample simple por decimación con acumulador
        // fraccional (suficiente para clasificación, no para reproducción).
        val ratio = sourceSampleRate.toDouble() / TARGET_SR
        var srcIdx = 0
        while (srcIdx < frames) {
            if (bufferFill >= YAMNET_INPUT_LENGTH) break
            val l = stereoInterleaved[srcIdx * 2]
            val r = stereoInterleaved[srcIdx * 2 + 1]
            monoBuffer[bufferFill] = (l + r) * 0.5f
            bufferFill++
            resampleAccumulator += ratio
            srcIdx += resampleAccumulator.toInt().coerceAtLeast(1)
            resampleAccumulator -= resampleAccumulator.toInt()
        }

        if (bufferFill >= YAMNET_INPUT_LENGTH) {
            val frameToClassify = monoBuffer.copyOf(YAMNET_INPUT_LENGTH)
            bufferFill = 0
            // Clasificación en corrutina aparte: TFLite tarda unos ms,
            // no debe bloquear el loop de decodificación/escritura de
            // IvannaBridgePlayer.
            if (classifyJob?.isActive != true) {
                classifyJob = scope.launch {
                    try {
                        val result = classifier.classify(frameToClassify)
                        if (result.isValid) {
                            val raw = if (result.speech > SPEECH_THRESHOLD) result.speech else 0f
                            smoothedScore += EMA_ALPHA * (raw - smoothedScore)
                            DSPBridge.setVoiceProtectScore(smoothedScore)
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "Error clasificando: ${e.message}")
                    }
                }
            }
        }
    }

    fun release() {
        enabled = false
        classifyJob?.cancel()
        classifier.release()
        DSPBridge.setVoiceProtectScore(0f)
    }

    /** Shutdown explícito — llamar desde onDestroy() para liberar Yamnet. */
    fun shutdown() { release() }
}
