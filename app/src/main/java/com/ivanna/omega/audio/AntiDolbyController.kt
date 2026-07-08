package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.YamnetClassifier
import com.ivanna.omega.core.AntiDolbyPreset
import kotlinx.coroutines.*
import kotlin.math.*

/**
 * AntiDolbyController — Orquestador de análisis de audio en tiempo real.
 *
 * Responsabilidades:
 * 1. Instanciar y mantener YamnetClassifier (modelo TFLite YAMNet)
 * 2. Procesar frames de audio periódicamente (cada ~100ms)
 * 3. Calcular scores de voz, música, bajos desde clasificación
 * 4. Llamar nativeSetAntiDolbyScoresStatic con los scores reales
 * 5. Ajustar parámetros del AudioEngine dinámicamente según el contenido
 * 6. Mantener fallback graceful si YAMNet no está disponible
 *
 * Flujo:
 *   Input stream → AudioCallbackManager → Anti-Dolby buffer (0.96s @ 16kHz)
 *   → YamnetClassifier.classify() → scores (voz, música, bajos)
 *   → AudioEngine.nativeSetAntiDolbyScoresStatic() → orquestador C++
 *   → audio_orchestrator.cpp adapta widener, EQ, compresor en tiempo real
 */
class AntiDolbyController(private val context: Context) {
    companion object {
        private const val TAG = "AntiDolbyController"

        // YAMNet espera 15600 samples @ 16kHz = 0.975s (≈1s útil)
        private const val YAMNET_INPUT_LENGTH = 15600
        private const val YAMNET_SAMPLE_RATE = 16000

        // Thread de procesamiento dedicado (cada 100ms = tiempo real práctico)
        private const val CLASSIFICATION_INTERVAL_MS = 100L

        // v3.1 PAR 2 tuning:
        // Histéresis para evitar oscilación cerca de las fronteras (voz/música/bajos):
        // una vez detectado un dominio, se necesita AT_ENTER para entrar,
        // pero sólo AT_EXIT (menor) para salir. Con eso frenamos el chatter
        // típico cuando la clasificación titubea entre 0.55 y 0.65.
        private const val DOMINANT_AT_ENTER_SPEECH = 0.60f
        private const val DOMINANT_AT_EXIT_SPEECH  = 0.48f
        private const val DOMINANT_AT_ENTER_MUSIC  = 0.60f
        private const val DOMINANT_AT_EXIT_MUSIC   = 0.48f
        private const val DOMINANT_AT_ENTER_BASS   = 0.40f
        private const val DOMINANT_AT_EXIT_BASS    = 0.30f

        // Rampa suave: factor de mezcla exponencial (alpha) hacia el target.
        // alpha=0.25 ≈ tiempo de ataque ~400ms con clasificación cada 100ms.
        private const val PARAM_RAMP_ALPHA = 0.25f
    }

    // v3.1 PAR 2: estado con histéresis y valores actuales (ramp) para
    // suavizar los ajustes dinámicos de exciter/width/eqGain.
    private enum class Domain { NONE, SPEECH, MUSIC, BASS }
    private var currentDomain: Domain = Domain.NONE
    private var curExciter: Float = 0.4f
    private var curWidth: Float = 0.5f
    private var curEqGain: Float = 0.0f

    private var yamnetClassifier: YamnetClassifier? = null
    private var audioEngine: AudioEngine? = null
    private val antiDolbyPreset = AntiDolbyPreset()
    
    private var classificationJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.Default + Job())
    
    private var isInitialized = false
    private var isAntiDolbyEnabled = false
    
    // Buffer circular para acumular frames @ 16kHz
    private var audioBuffer: FloatArray? = null
    private var bufferIndex = 0

    /**
     * Inicializa YamnetClassifier y AudioEngine.
     * Seguro llamar múltiples veces (solo inicializa una vez).
     */
    fun initialize(audioEngine: AudioEngine) {
        if (isInitialized) {
            Log.d(TAG, "Ya inicializado, ignorando reinicialización")
            return
        }

        try {
            // 1. Instanciar YamnetClassifier con modelo TFLite
            yamnetClassifier = YamnetClassifier(context)
            Log.i(TAG, "YamnetClassifier instanciado correctamente")
            
            // 2. Guardar referencia a AudioEngine
            this.audioEngine = audioEngine
            
            // 3. Inicializar buffer circular
            audioBuffer = FloatArray(YAMNET_INPUT_LENGTH)
            bufferIndex = 0
            
            isInitialized = true
            Log.i(TAG, "AntiDolbyController inicializado")
        } catch (e: Exception) {
            Log.e(TAG, "Error inicializando AntiDolbyController: ${e.message}")
            isInitialized = false
        }
    }

    /**
     * Habilita el sistema Anti-Dolby adaptativo.
     * Inicia el job de clasificación periódica.
     */
    fun enableAntiDolby() {
        if (!isInitialized || isAntiDolbyEnabled) {
            return
        }

        if (yamnetClassifier == null) {
            Log.w(TAG, "YamnetClassifier no disponible, Anti-Dolby deshabilitado")
            return
        }

        isAntiDolbyEnabled = true
        Log.i(TAG, "Anti-Dolby adaptativo habilitado")
        
        // Iniciar job de clasificación periódica
        startClassificationLoop()
    }

    /**
     * Deshabilita el sistema Anti-Dolby adaptativo.
     * Cancela el job de clasificación y resetea parámetros.
     */
    fun disableAntiDolby() {
        if (!isAntiDolbyEnabled) {
            return
        }

        isAntiDolbyEnabled = false
        classificationJob?.cancel()
        classificationJob = null

        // v3.1 PAR 2: al deshabilitar reseteamos histéresis + estado de ramp
        // para que la próxima activación no arranque desde el último dominio.
        currentDomain = Domain.NONE
        curExciter = 0.4f
        curWidth = 0.5f
        curEqGain = 0.0f

        // Resetear scores a cero (parámetros vuelven a valores por defecto)
        AudioEngine.nativeSetAntiDolbyScoresStatic(0f, 0f, 0f)
        
        Log.i(TAG, "Anti-Dolby adaptativo deshabilitado")
    }

    /**
     * Procesa un frame de audio.
     * Acumula datos en el buffer circular y ejecuta clasificación cuando está lleno.
     *
     * @param audioFrame Array de samples @ 16kHz, mono (puede ser < YAMNET_INPUT_LENGTH)
     */
    fun processAudioFrame(audioFrame: FloatArray) {
        if (!isAntiDolbyEnabled || audioBuffer == null) {
            return
        }

        val buffer = audioBuffer ?: return
        
        // Escribir frame en buffer circular
        var src = 0
        var remaining = audioFrame.size
        
        while (remaining > 0) {
            val canWrite = minOf(remaining, YAMNET_INPUT_LENGTH - bufferIndex)
            System.arraycopy(audioFrame, src, buffer, bufferIndex, canWrite)
            
            bufferIndex += canWrite
            src += canWrite
            remaining -= canWrite
            
            // Si buffer está lleno, ejecutar clasificación
            if (bufferIndex >= YAMNET_INPUT_LENGTH) {
                classifyBuffer(buffer)
                bufferIndex = 0
            }
        }
    }

    /**
     * Clasifica el buffer actual y actualiza AudioEngine.
     */
    private fun classifyBuffer(buffer: FloatArray) {
        val classifier = yamnetClassifier ?: return
        
        try {
            val result = classifier.classify(buffer)
            
            if (!result.isValid) {
                Log.d(TAG, "Clasificación no válida (fallback model?), usando scores neutros")
                AudioEngine.nativeSetAntiDolbyScoresStatic(0.5f, 0.5f, 0.5f)
                return
            }

            // Normalizar scores: sumar a 1.0 para que sean pesos
            val totalScore = result.speech + result.music + result.bass
            val normFactor = if (totalScore > 0.01f) 1f / totalScore else 0f
            
            val normSpeech = result.speech * normFactor
            val normMusic = result.music * normFactor
            val normBass = result.bass * normFactor
            val normSilence = (1f - totalScore).coerceIn(0f, 1f)
            
            // Llamar a C++ con scores normalizados
            AudioEngine.nativeSetAntiDolbyScoresStatic(
                normSpeech, normMusic, normBass
            )
            
            Log.d(TAG, String.format(
                "Yamnet: speech=%.3f, music=%.3f, bass=%.3f, silence=%.3f",
                normSpeech, normMusic, normBass, normSilence
            ))
            
            // Ajustar parámetros dinámicamente según clasificación
            adjustParameters(normSpeech, normMusic, normBass)
            
        } catch (e: Exception) {
            Log.e(TAG, "Error clasificando buffer: ${e.message}")
        }
    }

    /**
     * Ajusta parámetros del AudioEngine según clasificación.
     *
     * v3.1 PAR 2 tuning:
     *   - Histéresis (AT_ENTER > AT_EXIT) sobre el score dominante para
     *     eliminar el chatter que producía saltos de preset cerca de 0.6.
     *   - Rampa exponencial suave (alpha=0.25 por clasificación) hacia el
     *     target — los cambios ya no son escalones de un frame, se sienten
     *     como transiciones naturales de ~400 ms.
     *   - El dominio previo se mantiene si nadie supera su AT_ENTER, por
     *     lo que la escena "silencio débil" no rompe el preset activo.
     */
    private fun adjustParameters(speech: Float, music: Float, bass: Float) {
        val engine = audioEngine ?: return
        try {
            // 1) Decidir nuevo dominio con histéresis.
            val newDomain = when (currentDomain) {
                Domain.SPEECH -> when {
                    speech >= DOMINANT_AT_EXIT_SPEECH -> Domain.SPEECH  // sigue en SPEECH
                    music >= DOMINANT_AT_ENTER_MUSIC -> Domain.MUSIC
                    bass  >= DOMINANT_AT_ENTER_BASS  -> Domain.BASS
                    else -> Domain.NONE
                }
                Domain.MUSIC -> when {
                    music >= DOMINANT_AT_EXIT_MUSIC -> Domain.MUSIC
                    speech >= DOMINANT_AT_ENTER_SPEECH -> Domain.SPEECH
                    bass  >= DOMINANT_AT_ENTER_BASS   -> Domain.BASS
                    else -> Domain.NONE
                }
                Domain.BASS -> when {
                    bass  >= DOMINANT_AT_EXIT_BASS  -> Domain.BASS
                    speech >= DOMINANT_AT_ENTER_SPEECH -> Domain.SPEECH
                    music  >= DOMINANT_AT_ENTER_MUSIC  -> Domain.MUSIC
                    else -> Domain.NONE
                }
                Domain.NONE -> when {
                    speech >= DOMINANT_AT_ENTER_SPEECH -> Domain.SPEECH
                    music  >= DOMINANT_AT_ENTER_MUSIC  -> Domain.MUSIC
                    bass   >= DOMINANT_AT_ENTER_BASS   -> Domain.BASS
                    else -> Domain.NONE
                }
            }
            if (newDomain != currentDomain) {
                Log.i(TAG, "Anti-Dolby dominio: $currentDomain → $newDomain")
                currentDomain = newDomain
            }

            // 2) Fijar TARGETS del dominio activo (no aplicamos directo, rampeamos).
            val (tgtExciter, tgtWidth, tgtEq) = when (newDomain) {
                Domain.SPEECH -> Triple(0.20f, 0.30f,  0.0f)
                Domain.MUSIC  -> Triple(0.60f, 0.70f,  3.0f)
                Domain.BASS   -> Triple(0.30f, 0.50f, -2.0f)
                Domain.NONE   -> Triple(0.40f, 0.50f,  0.0f)
            }

            // 3) Ramp exponencial hacia el target y aplicar.
            curExciter += (tgtExciter - curExciter) * PARAM_RAMP_ALPHA
            curWidth   += (tgtWidth   - curWidth)   * PARAM_RAMP_ALPHA
            curEqGain  += (tgtEq      - curEqGain)  * PARAM_RAMP_ALPHA

            engine.setExciter(curExciter)
            engine.setWidth(curWidth)
            engine.setEqGain(curEqGain)
        } catch (e: Exception) {
            Log.e(TAG, "Error ajustando parámetros: ${e.message}")
        }
    }

    /**
     * Inicia el loop de clasificación periódica (fallback si no hay input directo).
     * Se ejecuta cada 100ms en background.
     */
    private fun startClassificationLoop() {
        classificationJob?.cancel()
        classificationJob = scope.launch {
            try {
                while (isActive && isAntiDolbyEnabled) {
                    // Cada 100ms, si hay datos en buffer, clasificar
                    if (bufferIndex > YAMNET_INPUT_LENGTH / 2) {
                        // Buffer al menos medio lleno: procesar
                        val buffer = audioBuffer ?: break
                        classifyBuffer(buffer)
                        bufferIndex = 0
                    }
                    delay(CLASSIFICATION_INTERVAL_MS)
                }
            } catch (e: CancellationException) {
                Log.d(TAG, "Classification loop cancelado")
            } catch (e: Exception) {
                Log.e(TAG, "Error en classification loop: ${e.message}")
            }
        }
    }

    /**
     * Libera recursos.
     * Llama a esto en onDestroy del Activity o cuando termina la aplicación.
     */
    fun release() {
        if (!isInitialized) {
            return
        }

        disableAntiDolby()
        classificationJob?.cancel()
        scope.cancel()
        
        yamnetClassifier?.release()
        yamnetClassifier = null
        
        audioBuffer = null
        isInitialized = false
        
        Log.i(TAG, "AntiDolbyController liberado")
    }
}
