package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.YamnetClassifier
import com.ivanna.omega.dsp.DSPBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

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

        // 3I: StateFlow global del score de voz — actualizado por cualquier instancia
        // activa de VoiceProtectionController; collectado en AdaptiveEngineScreen.
        private val _sharedVoiceScore = MutableStateFlow(0f)
        val sharedVoiceScore: StateFlow<Float> = _sharedVoiceScore.asStateFlow()
    }

    private val classifier = YamnetClassifier(context)
    private val scope = CoroutineScope(Dispatchers.Default + Job())
    private val classifierMutex = Mutex()
    private val inferenceLock = Any()
    private var inferenceGeneration = 0L
    private var classifyJob: Job? = null

    private var resampleAccumulator = 0.0
    private val monoBuffer = FloatArray(YAMNET_INPUT_LENGTH)
    private var bufferFill = 0
    private var smoothedScore = 0f

    // Parche 4: score de voice protection observable desde el exterior
    private val _voiceScore = MutableStateFlow(0f)
    val voiceScore: StateFlow<Float> = _voiceScore.asStateFlow()

    @Volatile var enabled = true

    // ── Modo manual (mejora pedida: activación sin depender de servicios
    // externos) ─────────────────────────────────────────────────────────────
    // El modo automático depende de YamnetClassifier (TFLite) detectando
    // voz en tiempo real — si el usuario simplemente quiere protección de
    // voz SIEMPRE activa (llamada, grabación, contenido hablado que YAMNet
    // no reconoce bien), no debería depender de esa clasificación. En modo
    // manual, feed() ni siquiera acumula/clasifica — el score se aplica
    // directo y una sola vez al activar, sin gasto de CPU por bloque.
    @Volatile private var manualModeEnabled = false

    /**
     * Activa o desactiva el modo manual. En manual, [score] (0..1, 1 =
     * protección máxima) se aplica de inmediato al camino real de audio
     * sin pasar por YamnetClassifier. Al desactivar, se limpia el score
     * suavizado para que el modo automático no arranque con un salto.
     */
    fun setManualMode(active: Boolean, score: Float = 1.0f) {
        synchronized(inferenceLock) {
            val transitioned = manualModeEnabled != active
            if (transitioned) {
                // Invalidate first: a TFLite/native classify() already running
                // may finish after Job.cancel() because that call is blocking.
                inferenceGeneration++
                classifyJob?.cancel()
                classifyJob = null
                bufferFill = 0
                resampleAccumulator = 0.0
                manualModeEnabled = active
            }

            if (active) {
                smoothedScore = score.coerceIn(0f, 1f)
                // Explicit manual score is a valid user control, not an
                // inference result. It is invalidated when leaving manual mode.
                DSPBridge.setVoiceProtectScore(smoothedScore)
                _voiceScore.value = smoothedScore
                _sharedVoiceScore.value = smoothedScore
                Log.i(TAG, "Modo manual activado, score=$smoothedScore")
            } else if (transitioned) {
                smoothedScore = 0f
                DSPBridge.clearVoiceProtectScore()
                _voiceScore.value = 0f
                _sharedVoiceScore.value = 0f
                Log.i(TAG, "Modo manual desactivado — score inválido/neutro hasta la siguiente inferencia YAMNet")
            }
        }
    }

    val isManualModeActive: Boolean get() = manualModeEnabled

    /**
     * Alimenta un bloque estéreo intercalado recién decodificado (mismo
     * formato que recibe DSPBridge.process()) para análisis de voz. No
     * modifica el audio; es solo para clasificación.
     */
    fun feed(stereoInterleaved: FloatArray, frames: Int, sourceSampleRate: Int) {
        if (frames <= 0 || sourceSampleRate <= 0) return

        synchronized(inferenceLock) {
            if (!enabled || manualModeEnabled) return
            val safeFrames = minOf(frames, stereoInterleaved.size / 2)
            if (safeFrames <= 0) return

            // Downmix to mono and resample for classification only.
            val ratio = sourceSampleRate.toDouble() / TARGET_SR
            var srcIdx = 0
            while (srcIdx < safeFrames && bufferFill < YAMNET_INPUT_LENGTH) {
                val l = stereoInterleaved[srcIdx * 2]
                val r = stereoInterleaved[srcIdx * 2 + 1]
                monoBuffer[bufferFill++] = (l + r) * 0.5f
                resampleAccumulator += ratio
                val advance = resampleAccumulator.toInt()
                srcIdx += advance.coerceAtLeast(1)
                resampleAccumulator -= advance
            }

            if (bufferFill >= YAMNET_INPUT_LENGTH && classifyJob?.isActive != true) {
                val frameToClassify = monoBuffer.copyOf(YAMNET_INPUT_LENGTH)
                bufferFill = 0
                val generation = inferenceGeneration

                classifyJob = scope.launch {
                    try {
                        // Serialize calls to the same TFLite interpreter. A
                        // cancelled blocking inference may still be executing;
                        // a newer inference must not enter that interpreter
                        // concurrently.
                        classifierMutex.withLock {
                            val result = classifier.classify(frameToClassify)
                            if (result.isValid) {
                                synchronized(inferenceLock) {
                                    if (generation != inferenceGeneration ||
                                        manualModeEnabled || !enabled) {
                                        return@withLock
                                    }
                                    val raw = if (result.speech > SPEECH_THRESHOLD) result.speech else 0f
                                    smoothedScore += EMA_ALPHA * (raw - smoothedScore)
                                    DSPBridge.setVoiceProtectScore(smoothedScore)
                                    _voiceScore.value = smoothedScore
                                    _sharedVoiceScore.value = smoothedScore
                                }
                            }
                        }
                    } catch (e: kotlinx.coroutines.CancellationException) {
                        throw e
                    } catch (e: Exception) {
                        Log.w(TAG, "Error clasificando: ${e.message}")
                    }
                }
            }
        }
    }

    fun release() {
        synchronized(inferenceLock) {
            inferenceGeneration++
            enabled = false
            manualModeEnabled = false
            classifyJob?.cancel()
            classifyJob = null
            bufferFill = 0
            resampleAccumulator = 0.0
            smoothedScore = 0f
            DSPBridge.clearVoiceProtectScore()
            _voiceScore.value = 0f
            _sharedVoiceScore.value = 0f
        }
        // classify() may be a blocking native/TFLite call and can outlive its
        // cancelled Job. Queue interpreter release behind that call instead
        // of racing classifier.release() against an in-flight invocation.
        scope.launch {
            classifierMutex.withLock {
                classifier.release()
            }
            scope.coroutineContext[Job]?.cancel()
        }
    }
}
