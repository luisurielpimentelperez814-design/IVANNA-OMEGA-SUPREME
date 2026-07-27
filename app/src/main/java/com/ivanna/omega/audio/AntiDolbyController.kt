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
        private const val EMA_IN  = 0.25f
        private const val EMA_OUT = 0.18f
    }

    private var yamnetClassifier: YamnetClassifier? = null
    var onDspUpdate: ((exciter: Float, width: Float, eqGainDb: Float) -> Unit)? = null
    private var emaSpeech = 0f
    private var emaMusic  = 0f
    private var emaBass   = 0f
    private var smoothExciter = 0.32f
    private var smoothWidth   = 0.50f
    private var smoothEq      = 0.00f
    private val antiDolbyPreset = AntiDolbyPreset()
    
    private var classificationJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.Default + Job())
    
    private var isInitialized = false
    private var isAntiDolbyEnabled = false
    
    // Buffer circular para acumular frames @ 16kHz
    private var audioBuffer: FloatArray? = null
    private var bufferIndex = 0
    private val bufferLock = Any()

    /**
     * Inicializa YamnetClassifier y AudioEngine.
     * Seguro llamar múltiples veces (solo inicializa una vez).
     */
    fun initialize() {
        if (isInitialized) {
            Log.d(TAG, "Ya inicializado, ignorando reinicialización")
            return
        }

        try {
            // 1. Instanciar YamnetClassifier con modelo TFLite
            yamnetClassifier = YamnetClassifier(context)
            Log.i(TAG, "YamnetClassifier instanciado correctamente")
            
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
        CinematicEngineHost.start(context, sampleRate = AudioPipeline.SAMPLE_RATE)
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
        
        // Resetear scores a cero (parámetros vuelven a valores por defecto)
        AudioEngine.nativeSetAntiDolbyScoresStatic(0f, 0f, 0f)
        emaSpeech = 0f; emaMusic = 0f; emaBass = 0f
        smoothExciter = 0.32f; smoothWidth = 0.50f; smoothEq = 0f
        onDspUpdate?.invoke(0.32f, 0.50f, 0f)
        CinematicEngineHost.stop()
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

        // FIX (crash HFTR): serializar lecturas/escrituras del buffer
        synchronized(bufferLock) {
            val buffer = audioBuffer ?: return
            var src = 0
            var remaining = audioFrame.size
            while (remaining > 0) {
                val space = YAMNET_INPUT_LENGTH - bufferIndex
                if (space <= 0) { bufferIndex = 0; continue }
                val canWrite = minOf(remaining, space, audioFrame.size - src)
                if (canWrite <= 0) break
                System.arraycopy(audioFrame, src, buffer, bufferIndex, canWrite)
                bufferIndex += canWrite
                src += canWrite
                remaining -= canWrite
                if (bufferIndex >= YAMNET_INPUT_LENGTH) {
                    val snapshot = buffer.copyOf()
                    bufferIndex = 0
                    scope.launch { classifyBuffer(snapshot) }
                }
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
     * Lógica:
     * - Si voz > 60%: reducir exciter y widener (preservar claridad)
     * - Si música > 60%: aumentar exciter y ancho (enriquecer)
     * - Si bajos > 40%: aplicar compresor más agresivo (control dinámico)
     * - Si silencio > 60%: resetear a defaults
     */
    private fun adjustParameters(speech: Float, music: Float, bass: Float) {
        if (onDspUpdate == null) return

        emaSpeech += EMA_IN * (speech - emaSpeech)
        emaMusic  += EMA_IN * (music  - emaMusic)
        emaBass   += EMA_IN * (bass   - emaBass)
        val emaSilence = (1f - emaSpeech - emaMusic - emaBass).coerceIn(0f, 1f)

        val tExciter = emaSpeech * 0.12f + emaMusic * 0.68f + emaBass * 0.26f + emaSilence * 0.32f
        val tWidth   = emaSpeech * 0.22f + emaMusic * 0.78f + emaBass * 0.42f + emaSilence * 0.50f
        val tEq      = emaSpeech * (-1.5f) + emaMusic * 3.5f + emaBass * (-3.5f)

        smoothExciter += EMA_OUT * (tExciter - smoothExciter)
        smoothWidth   += EMA_OUT * (tWidth   - smoothWidth)
        smoothEq      += EMA_OUT * (tEq      - smoothEq)

        onDspUpdate!!.invoke(smoothExciter, smoothWidth, smoothEq)

        Log.v(TAG, "adj exc=%.3f wid=%.3f eq=%.2fdB [sp=%.2f mu=%.2f ba=%.2f si=%.2f]"
            .format(smoothExciter, smoothWidth, smoothEq,
                    emaSpeech, emaMusic, emaBass, emaSilence))
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
                    // FIX (crash HFTR): tomar snapshot bajo lock, procesar fuera
                    val snapshot: FloatArray? = synchronized(bufferLock) {
                        val buf = audioBuffer
                        if (buf != null && bufferIndex > YAMNET_INPUT_LENGTH / 2) {
                            val copy = buf.copyOf()
                            bufferIndex = 0
                            copy
                        } else null
                    }
                    if (snapshot != null) {
                        runCatching { classifyBuffer(snapshot) }
                            .onFailure { Log.w(TAG, "classify loop: ${it.message}") }
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
        
        CinematicEngineHost.stop()
        Log.i(TAG, "AntiDolbyController liberado")
    }
}
