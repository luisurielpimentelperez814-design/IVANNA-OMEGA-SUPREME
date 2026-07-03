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
    }

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
        
        // Resetear scores a cero (parámetros vuelven a valores por defecto)
        AudioEngine.nativeSetAntiDolbyScoresStatic(0f, 0f, 0f, 1f)
        
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
                AudioEngine.nativeSetAntiDolbyScoresStatic(0.5f, 0.5f, 0.5f, 0f)
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
                normSpeech, normMusic, normBass, normSilence
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
        audioEngine ?: return
        
        try {
            when {
                speech > 0.6f -> {
                    // Domina voz: preservar claridad, reducir efectos
                    audioEngine!!.setExciter(0.2f)
                    audioEngine!!.setWidth(0.3f)
                    audioEngine!!.setEqGain(0f)
                }
                music > 0.6f -> {
                    // Domina música: enriquecer con exciter y ancho
                    audioEngine!!.setExciter(0.6f)
                    audioEngine!!.setWidth(0.7f)
                    audioEngine!!.setEqGain(3f)
                }
                bass > 0.4f -> {
                    // Bajos presentes: aplicar compresión sin distorsión
                    audioEngine!!.setExciter(0.3f)
                    audioEngine!!.setWidth(0.5f)
                    audioEngine!!.setEqGain(-2f)  // Reducir ligeramente bajos extremos
                }
                else -> {
                    // Modo default / silencio
                    audioEngine!!.setExciter(0.4f)
                    audioEngine!!.setWidth(0.5f)
                    audioEngine!!.setEqGain(0f)
                }
            }
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
