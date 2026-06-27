package com.ivannafusion

import android.content.Context
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import kotlinx.coroutines.*
import kotlin.math.*

/**
 * AudioEngine — Motor de audio DSP.
 *
 * CORREGIDO: la versión anterior de este archivo tenía 3 bloques de
 * "stubs" duplicados (nativeSetCompressorBypass/Knee/Makeup repetidos
 * 3 veces) escritos FUERA del cierre de la clase ('}' en la línea 292,
 * con código suelto hasta la línea 410) — eso es un error de
 * compilación garantizado en Kotlin (no puedes tener funciones a nivel
 * de archivo con el mismo nombre, y el código tras el '}' de la clase
 * quedaba huérfano). Esta es la causa más probable del "desastre"/
 * crash reportado: el proyecto literalmente no compilaba.
 *
 * Esta reconstrucción: una sola clase, sin duplicados, con las
 * funciones JNI reales conectadas a jni_wrapper.cpp (el único archivo
 * que compila en el target 'ivanna_jni', ver CMakeLists.txt).
 */
class AudioEngine {
    companion object {
        private const val TAG = "AudioEngine"
        private const val SAMPLE_RATE = 48000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_STEREO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_FLOAT

        /** Suavizado exponencial (filtro de un polo) — ver historial del
         *  proyecto para la verificación de que es equivalente a un EMA. */
        fun homeostasis(n: Float, omega: Float, mu: Float = 0.3f): Float {
            if (omega.isNaN() || omega.isInfinite()) return n
            if (n.isNaN() || n.isInfinite()) return omega
            return (n + mu * omega) / (1.0f + mu)
        }
    }

    private var audioRecord: AudioRecord? = null
    private var processingJob: Job? = null
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    private val eqGains = FloatArray(8) { 0f }
    private var compThreshold = -20f
    private var compRatio = 4f
    private var exciterDrive = 1f

    @Volatile private var homeostaticRmsDb = -60f
    @Volatile private var homeostaticSpectrum = FloatArray(32)
    @Volatile private var homeostaticCorrelation = 1f
    @Volatile private var homeostaticLatencyMicros = 5000
    @Volatile private var homeostaticGeneration = 0
    @Volatile private var homeostaticFitness = 0f
    @Volatile private var homeostaticTempo = 120f
    @Volatile private var homeostaticGenre = "ROCK"

    private val spectrumHistory = ArrayDeque<FloatArray>()
    private val onsetHistory = ArrayDeque<Long>()

    init {
        try {
            System.loadLibrary("ivanna_jni")
            Log.i(TAG, "Librería nativa cargada")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Error cargando librería nativa", e)
        }
    }

    fun initialize(context: Context, callback: (Boolean) -> Unit) {
        scope.launch {
            try {
                val bufferSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
                if (bufferSize <= 0) {
                    callback(false)
                    return@launch
                }

                audioRecord = AudioRecord(
                    MediaRecorder.AudioSource.MIC,
                    SAMPLE_RATE,
                    CHANNEL_CONFIG,
                    AUDIO_FORMAT,
                    bufferSize * 2
                )

                if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                    callback(false)
                    return@launch
                }

                try {
                    nativeInit(SAMPLE_RATE, 2)
                } catch (e: Exception) {
                    Log.w(TAG, "Native init falló: ${e.message}")
                }

                audioRecord?.startRecording()
                startProcessing()
                callback(true)
            } catch (e: Exception) {
                Log.e(TAG, "Error en inicialización", e)
                callback(false)
            }
        }
    }

    private fun startProcessing() {
        processingJob = scope.launch {
            val buffer = FloatArray(2048)
            val outputBuffer = FloatArray(2048)
            var frameCounter = 0

            while (isActive) {
                val read = audioRecord?.read(buffer, 0, buffer.size, android.media.AudioRecord.READ_BLOCKING) ?: 0
                if (read > 0) {
                    try {
                        nativeProcessAudio(buffer, outputBuffer, read / 2)
                    } catch (e: Exception) {
                        Log.w(TAG, "Error en procesamiento: ${e.message}")
                    }

                    frameCounter++
                    if (frameCounter % 10 == 0) {
                        updateMetrics(buffer, read)
                    }
                }
                delay(10)
            }
        }
    }

    private suspend fun updateMetrics(buffer: FloatArray, size: Int) {
        withContext(Dispatchers.Default) {
            try {
                var sum = 0f
                for (i in 0 until size) sum += buffer[i] * buffer[i]
                val rms = sqrt(sum / size)
                val rawRms = if (rms > 0.0001f) 20 * log10(rms).toFloat() else -60f
                homeostaticRmsDb = homeostasis(homeostaticRmsDb, rawRms, 0.3f)

                val rawSpectrum = calculateFFT(buffer, size)
                homeostaticSpectrum = FloatArray(32) { i ->
                    val adaptiveMu = 0.3f * (1.0f + abs(rawSpectrum[i] - homeostaticSpectrum[i]))
                    homeostasis(homeostaticSpectrum[i], rawSpectrum[i], adaptiveMu)
                }

                val rawCorrelation = calculateCorrelation(buffer, size)
                homeostaticCorrelation = homeostasis(homeostaticCorrelation, rawCorrelation, 0.2f)

                val rawBpm = detectBPM(buffer, size)
                if (rawBpm > 0) {
                    homeostaticTempo = homeostasis(homeostaticTempo, rawBpm, 0.1f)
                }

                val rawGenre = detectGenre(homeostaticSpectrum)
                if (rawGenre == homeostaticGenre || Math.random() > 0.7) {
                    homeostaticGenre = rawGenre
                } else {
                    // Mantener el género actual
                }
            } catch (e: Exception) {
                Log.w(TAG, "Error actualizando métricas: ${e.message}")
            }
        }
    }

    private fun calculateFFT(buffer: FloatArray, size: Int): FloatArray {
        val spectrum = FloatArray(32)
        val fftSize = minOf(512, size)

        for (band in 0 until 32) {
            val freq = 20f * (1000f / 20f).pow(band / 31f)
            val k = (freq * fftSize / SAMPLE_RATE).toInt().coerceIn(1, fftSize / 2 - 1)

            var real = 0f
            var imag = 0f
            for (n in 0 until fftSize) {
                val angle = 2 * PI * k * n / fftSize
                real += buffer[n] * cos(angle).toFloat()
                imag -= buffer[n] * sin(angle).toFloat()
            }

            val magnitude = sqrt(real * real + imag * imag) / fftSize
            spectrum[band] = magnitude.coerceIn(0f, 1f)
        }

        spectrumHistory.addLast(spectrum.copyOf())
        if (spectrumHistory.size > 5) spectrumHistory.removeFirst()

        val smoothed = FloatArray(32)
        for (i in 0 until 32) {
            var sum = 0f
            spectrumHistory.forEach { sum += it[i] }
            smoothed[i] = sum / spectrumHistory.size
        }

        return smoothed
    }

    private fun calculateCorrelation(buffer: FloatArray, size: Int): Float {
        var sumL = 0f
        var sumR = 0f
        var sumLR = 0f

        for (i in 0 until size - 1 step 2) {
            val L = buffer[i]
            val R = buffer[i + 1]
            sumL += L * L
            sumR += R * R
            sumLR += L * R
        }

        val denom = sqrt(sumL * sumR)
        return if (denom > 0.0001f) (sumLR / denom).coerceIn(-1f, 1f) else 1f
    }

    private fun detectBPM(buffer: FloatArray, size: Int): Float {
        val now = System.currentTimeMillis()
        val energy = buffer.take(size).sumOf { (it * it).toDouble() }.toFloat() / size

        if (energy > 0.01f) {
            onsetHistory.addLast(now)
            if (onsetHistory.size > 20) onsetHistory.removeFirst()
        }
        if (onsetHistory.size >= 4) {
            val intervals = mutableListOf<Long>()
            for (i in 1 until onsetHistory.size) {
                intervals.add(onsetHistory[i] - onsetHistory[i - 1])
            }

            val avgInterval = intervals.average()
            if (avgInterval > 0) {
                val bpm = (60000.0 / avgInterval).toFloat()
                if (bpm in 60f..180f) return bpm
            }
        }
        return -1f
    }

    private fun detectGenre(spectrum: FloatArray): String {
        val lowEnergy = spectrum.take(8).average()
        val midEnergy = spectrum.slice(8..20).average()
        val highEnergy = spectrum.takeLast(11).average()

        return when {
            lowEnergy > 0.6 && midEnergy < 0.4 -> "BASS"
            highEnergy > 0.5 && lowEnergy < 0.3 -> "ELECTRONIC"
            midEnergy > 0.5 && lowEnergy > 0.3 -> "ROCK"
            highEnergy > 0.4 && midEnergy > 0.4 -> "POP"
            lowEnergy > 0.4 && highEnergy > 0.3 -> "JAZZ"
            else -> "UNKNOWN"
        }
    }

    // ── Getters de métricas ─────────────────────────────────────────────────
    fun aiGetRmsDb() = homeostaticRmsDb
    fun aiGetSpectrum() = homeostaticSpectrum
    fun getCorrelation() = homeostaticCorrelation
    fun getLatencyMicros() = homeostaticLatencyMicros
    fun getGeneration() = homeostaticGeneration
    fun getBestFitness() = homeostaticFitness
    fun aiGetDetectedGenre() = homeostaticGenre
    fun aiGetTempo() = homeostaticTempo

    // ── EQ (8 bandas: gain SÍ tenía efecto real; freq/Q estaban vacíos en
    //     C++ — corregido también en jni_wrapper.cpp en este mismo commit) ──
    fun eqSetGain(band: Int, gain: Float) {
        if (band in 0..7) {
            eqGains[band] = gain
            try { nativeSetEQGain(band, gain) } catch (e: Exception) {}
        }
    }
    fun eqSetFreq(band: Int, freqHz: Float) {
        if (band in 0..7) { try { nativeSetEQFreq(band, freqHz) } catch (e: Exception) {} }
    }
    fun eqSetQ(band: Int, q: Float) {
        if (band in 0..7) { try { nativeSetEQQ(band, q) } catch (e: Exception) {} }
    }
    fun eqSetBypass(band: Int, bypass: Boolean) {
        try { nativeSetEQBypass(band, bypass) } catch (e: Exception) {}
    }

    // ── Compresor ────────────────────────────────────────────────────────────
    fun compSetThreshold(threshold: Float) {
        compThreshold = threshold
        try { nativeSetCompressorThreshold(threshold) } catch (e: Exception) {}
    }
    fun compSetRatio(ratio: Float) {
        compRatio = ratio
        try { nativeSetCompressorRatio(ratio) } catch (e: Exception) {}
    }
    fun compSetAttack(attackMs: Float) { try { nativeSetCompressorAttack(attackMs) } catch (e: Exception) {} }
    fun compSetRelease(releaseMs: Float) { try { nativeSetCompressorRelease(releaseMs) } catch (e: Exception) {} }
    fun compSetKnee(kneeDb: Float) { try { nativeSetCompressorKnee(kneeDb) } catch (e: Exception) {} }
    fun compSetMakeup(makeupDb: Float) { try { nativeSetCompressorMakeup(makeupDb) } catch (e: Exception) {} }
    fun compSetBypass(bypass: Boolean) { try { nativeSetCompressorBypass(bypass) } catch (e: Exception) {} }

    // Alias retrocompatibles (algunas pantallas usaban estos nombres antes)
    fun setCompressorThreshold(threshold: Float) = compSetThreshold(threshold)
    fun setCompressorRatio(ratio: Float) = compSetRatio(ratio)

    // ── Excitador armónico ───────────────────────────────────────────────────
    fun excSetDrive(drive: Float) {
        exciterDrive = drive
        try { nativeSetExciterDrive(drive) } catch (e: Exception) {}
    }
    fun excSetMix(mix: Float) { try { nativeSetExciterMix(mix) } catch (e: Exception) {} }
    fun excSetBypass(bypass: Boolean) { try { nativeSetExciterBypass(bypass) } catch (e: Exception) {} }
    fun setExciterDrive(drive: Float) = excSetDrive(drive)

    // ── FFT effect (boost simple, ver jni_wrapper.cpp) ──────────────────────
    fun fftSetEnabled(enabled: Boolean) { try { nativeSetFFTEffect(enabled) } catch (e: Exception) {} }

    // ── IA (clasificador espectral simple — ver detectGenre/calculateFFT;
    //     NO es un modelo de machine learning entrenado, es heurística por
    //     energía de banda. Documentado así para no simular más de lo real) ──
    fun aiSetEnabled(enabled: Boolean) { aiEnabled = enabled }
    fun aiSetAutoAdapt(enabled: Boolean) { aiAutoAdapt = enabled }
    fun aiSetSensitivity(value: Float) { aiSensitivity = value.coerceIn(0f, 1f) }
    fun isAiClassifierLoaded(): Boolean = true  // heurística siempre disponible, sin modelo externo
    fun aiGetBassEnergy(): Float = homeostaticSpectrum.take(8).average().toFloat()
    fun aiGetMidEnergy(): Float = homeostaticSpectrum.slice(8..20).average().toFloat()
    fun aiGetHighEnergy(): Float = homeostaticSpectrum.takeLast(11).average().toFloat()
    fun aiGetCentroidHz(): Float {
        var weightedSum = 0f
        var totalEnergy = 0f
        for (i in homeostaticSpectrum.indices) {
            val freq = 20f * (1000f / 20f).pow(i / 31f)
            weightedSum += freq * homeostaticSpectrum[i]
            totalEnergy += homeostaticSpectrum[i]
        }
        return if (totalEnergy > 0.0001f) weightedSum / totalEnergy else 0f
    }
    fun aiGetZcr(): Float = 0f  // zero-crossing rate: requiere buffer crudo, no solo espectro promediado — pendiente real, no simulado con un valor inventado distinto de 0
    private var aiEnabled = false
    private var aiAutoAdapt = false
    private var aiSensitivity = 0.5f

    // ── Convolver / FDN Reverb — conectado a nativeReverbSet* en jni_wrapper.cpp ──
    fun convSetType(type: String) {
        convolverType = type
        try { nativeReverbSetType(type) } catch (e: Exception) {}
    }
    fun convSetDecay(value: Float) {
        convolverDecay = value
        try { nativeReverbSetDecay(value / 10f) } catch (e: Exception) {} // 0.1..10s → 0..1
    }
    fun convSetPreDelay(value: Float) {
        convolverPreDelay = value
        try { nativeReverbSetPreDelay(value) } catch (e: Exception) {}    // ms direct
    }
    fun convSetDamping(value: Float) {
        convolverDamping = value
        try { nativeReverbSetDamping(value) } catch (e: Exception) {}
    }
    fun convSetDiffusion(value: Float) {
        convolverDiffusion = value
        try { nativeReverbSetDiffusion(value) } catch (e: Exception) {}
    }
    fun convSetEarlyMix(value: Float) { convolverEarlyMix = value }
    fun convSetMix(value: Float) {
        convolverMix = value
        try { nativeReverbSetMix(value) } catch (e: Exception) {}
    }
    fun convPreset(presetId: Int) {
        val (type, decay, damp, diff, mix) = when (presetId) {
            0 -> listOf("ROOM",   2f,  0.6f, 0.5f, 0.25f)
            1 -> listOf("HALL",   8f,  0.4f, 0.8f, 0.35f)
            2 -> listOf("PLATE",  3f,  0.7f, 0.9f, 0.30f)
            3 -> listOf("SPRING", 1.5f,0.3f, 0.4f, 0.20f)
            else -> return
        }
        convSetType(type as String)
        convSetDecay(decay as Float)
        convSetDamping(damp as Float)
        convSetDiffusion(diff as Float)
        convSetMix(mix as Float)
    }
    fun convPresetSmallRoom() = convPreset(0)
    fun convPresetLargeHall() = convPreset(1)
    fun convPresetPlate()     = convPreset(2)
    fun convPresetSpring()    = convPreset(3)
    private var convolverType      = "HALL"
    private var convolverDecay     = 0.4f
    private var convolverPreDelay  = 0.1f
    private var convolverDamping   = 0.5f
    private var convolverDiffusion = 0.7f
    private var convolverEarlyMix  = 0.5f
    private var convolverMix       = 0.3f

    // ── Spatial / Stereo Widener — conectado a nativeWider*/nativeSpatial* ──
    fun spatialSetWidth(value: Float) {
        spatialWidth = value
        try { nativeWiderSetWidth(value) } catch (e: Exception) {}
        try { nativeSpatialSetWidth(value) } catch (e: Exception) {}
    }
    fun spatialSetDepth(value: Float) {
        spatialDepth = value
        try { nativeWiderSetDepth(value) } catch (e: Exception) {}
    }
    fun decorSetWidth(value: Float)     = spatialSetWidth(value)
    fun decorSetDepth(value: Float)     = spatialSetDepth(value)
    fun decorSetDiffusion(value: Float) {
        spatialDiffusion = value
        try { nativeSpatialSetMu(value) } catch (e: Exception) {}
    }
    fun decorSetDelay(value: Float) {
        spatialDelay = value
        try { nativeWiderSetDelay(value) } catch (e: Exception) {}
    }
    fun decorSetModRate(value: Float) { spatialModRate = value }
    fun decorSetMix(value: Float) {
        spatialMix = value
        try { nativeWiderSetMix(value) } catch (e: Exception) {}
    }
    fun decorPresetNatural()     { spatialSetWidth(1.0f); spatialSetDepth(0.3f); decorSetDiffusion(0.15f) }
    fun decorPresetWide()        { spatialSetWidth(1.8f); spatialSetDepth(0.6f); decorSetDiffusion(0.35f) }
    fun decorPresetMonoToStereo(){ spatialSetWidth(1.5f); spatialSetDepth(0.8f); decorSetDiffusion(0.50f) }
    private var spatialWidth     = 1.0f
    private var spatialDepth     = 0.5f
    private var spatialDiffusion = 0.3f
    private var spatialDelay     = 0.15f
    private var spatialModRate   = 0.5f
    private var spatialMix       = 1.0f

    // ── PF Engine — conectado a native PF en jni_wrapper.cpp ─────────────────
    fun pfSetParam(paramName: String, value: Float) {
        when (paramName) {
            "drive"     -> { DSPState.pfDrive     = value; try { nativePfSetDrive(value)    } catch (e: Exception) {} }
            "wet"       -> { DSPState.pfWet       = value; try { nativePfSetWet(value)      } catch (e: Exception) {} }
            "alpha"     -> { DSPState.pfAlpha     = value; try { nativePfSetAlpha(value)    } catch (e: Exception) {} }
            "beta"      -> { DSPState.pfBeta      = value }
            "delta"     -> { DSPState.pfDelta     = value; try { nativePfSetDelta(value)    } catch (e: Exception) {} }
            "sigma"     -> { DSPState.pfSigma     = value; try { nativePfSetSigma(value)    } catch (e: Exception) {} }
            "freq"      -> { DSPState.pfFreq      = value }
            "resonance" -> { DSPState.pfResonance = value }
            "mix"       -> { DSPState.pfMix       = value; try { nativePfSetWet(value)      } catch (e: Exception) {} }
            "low"       -> { DSPState.pfLowGain   = value; try { nativePfSetLow(value)      } catch (e: Exception) {} }
            "mid"       -> { DSPState.pfMidGain   = value; try { nativePfSetMid(value)      } catch (e: Exception) {} }
            "high"      -> { DSPState.pfHighGain  = value; try { nativePfSetHigh(value)     } catch (e: Exception) {} }
            "presence"  -> { DSPState.pfPresence  = value; try { nativePfSetPresence(value) } catch (e: Exception) {} }
            "amp_model" -> { DSPState.pfAmpModel  = value.toInt(); try { nativePfSetAmp(value.toInt()) } catch (e: Exception) {} }
            else        -> Log.w(TAG, "pfSetParam: desconocido '$paramName'")
        }
    }
    fun pfEvoStart()   { try { nativePfEvoStart() } catch (e: Exception) {} }
    fun pfEvoTick() { try { nativePfEvoTick(1) } catch (e: Exception) {} }
    fun applyPFPreset(presetName: String) { /* tabla de presets pendiente */ }
    fun pfSetAmp(modelIndex: Int) { pfSetParam("amp_model", modelIndex.toFloat()) }

    // ── Presets globales (solo lo que DSPState realmente expone) ───────────
    fun setPreset(presetName: String) {
        Log.i(TAG, "setPreset('$presetName') — aplicando valores conocidos")
        when (presetName.lowercase()) {
            "flat", "default" -> {
                for (b in 0..7) eqSetGain(b, 0f)
                compSetBypass(true)
                excSetBypass(true)
            }
            "rock" -> {
                eqSetGain(0, 4f); eqSetGain(1, 2f); eqSetGain(5, 3f); eqSetGain(6, 4f)
                compSetThreshold(-18f); compSetRatio(3f); compSetBypass(false)
            }
        }
    }

    fun release() {
        processingJob?.cancel()
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        scope.cancel()
        YamnetClassifier.release()
    }

    @Volatile private var lastYamnetResult: YamnetClassifier.Result? = null
    private val yamnetAccumulator = FloatArray(YamnetClassifier.INPUT_SAMPLES)
    private var yamnetAccumPos = 0
    private var yamnetClassifying = false

    /**
     * Punto de entrada para audio MONO ya capturado externamente (ver
     * PlaybackCaptureService — captura de audio interno del sistema vía
     * AudioPlaybackCapture). Acumula hasta tener exactamente
     * YamnetClassifier.INPUT_SAMPLES muestras y dispara una inferencia
     * en un hilo separado para no bloquear el hilo que llamó a esto.
     */
    fun feedExternalMonoAudio(monoSamples: FloatArray, sourceSampleRate: Int) {
        if (!YamnetClassifier.isLoaded) return
        // Decimación simple si la tasa de origen no es ya 16kHz.
        val ratio = (sourceSampleRate.toFloat() / YamnetClassifier.SAMPLE_RATE_HZ).coerceAtLeast(1f)
        var srcIdx = 0f
        while (srcIdx.toInt() < monoSamples.size && yamnetAccumPos < yamnetAccumulator.size) {
            yamnetAccumulator[yamnetAccumPos++] = monoSamples[srcIdx.toInt()]
            srcIdx += ratio
        }
        if (yamnetAccumPos >= yamnetAccumulator.size && !yamnetClassifying) {
            yamnetClassifying = true
            val block = yamnetAccumulator.copyOf()
            yamnetAccumPos = 0
            Thread {
                try {
                    val result = YamnetClassifier.classify(block)
                    if (result != null) lastYamnetResult = result
                } finally {
                    yamnetClassifying = false
                }
            }.apply { isDaemon = true; start() }
        }
    }

    /** Confianza real del último resultado de YAMNet (0 si no hay clasificador cargado o aún no clasificó nada). */
    fun aiGetConfidence(): Float = lastYamnetResult?.topScore?.coerceIn(0f, 1f) ?: 0f

    // ── Native methods — TODOS implementados realmente en jni_wrapper.cpp
    //     (verificado: grep de los símbolos Java_com_ivannafusion_AudioEngine_*
    //     contra el archivo fuente antes de declarar cada external fun aquí) ──
    private external fun nativeInit(sampleRate: Int, channels: Int): Boolean
    private external fun nativeProcessAudio(input: FloatArray, output: FloatArray, frames: Int)
    private external fun nativeSetEQGain(band: Int, gain: Float)
    private external fun nativeSetEQFreq(band: Int, freqHz: Float)
    private external fun nativeSetEQQ(band: Int, q: Float)
    private external fun nativeSetEQBypass(band: Int, bypass: Boolean)
    private external fun nativeSetCompressorThreshold(threshold: Float)
    private external fun nativeSetCompressorRatio(ratio: Float)
    private external fun nativeSetCompressorAttack(attackMs: Float)
    private external fun nativeSetCompressorRelease(releaseMs: Float)
    private external fun nativeSetCompressorKnee(kneeDb: Float)
    private external fun nativeSetCompressorMakeup(makeupDb: Float)
    private external fun nativeSetCompressorBypass(bypass: Boolean)
    private external fun nativeSetExciterDrive(drive: Float)
    private external fun nativeSetExciterMix(mix: Float)
    private external fun nativeSetExciterBypass(bypass: Boolean)
    private external fun nativeSetFFTEffect(enabled: Boolean)
    // PF-Engine methods (added by patch)
    fun updatePfDrive(value: Float) { DSPState.pfDrive = value }
    fun updatePfWet(value: Float) { DSPState.pfWet = value }
    fun updatePfAlpha(value: Float) { DSPState.pfAlpha = value }
    fun updatePfBeta(value: Float) { DSPState.pfBeta = value }
    fun updatePfDelta(value: Float) { DSPState.pfDelta = value }
    fun updatePfSigma(value: Float) { DSPState.pfSigma = value }
    fun updatePfFreq(value: Float) { DSPState.pfFreq = value }
    fun updatePfResonance(value: Float) { DSPState.pfResonance = value }
    fun updatePfMix(value: Float) { DSPState.pfMix = value }
    fun resetPfEvolution() { /* stub */ }
    // Métodos PF adicionales (añadidos por script)
    fun pfGetBarCount(): Int = try { nativePfGetBarCount() } catch (e: Exception) { 0 }
    fun pfEvoStop()    { try { nativePfEvoStop()  } catch (e: Exception) {} }
    fun pfEvoReset()   { try { nativePfEvoReset() } catch (e: Exception) {} }

    // ── Native declarations — PF Engine ───────────────────────────────────
    private external fun nativePfEvoStart()
    private external fun nativePfEvoTick(bars: Int)
    private external fun nativePfEvoStop()
    private external fun nativePfEvoReset()
    private external fun nativePfGetBarCount(): Int
    private external fun nativeGetGeneration(): Int
    private external fun nativeGetBestFitness(): Float
    private external fun nativePfSetAmp(i: Int)
    private external fun nativePfSetDrive(f: Float)
    private external fun nativePfSetWet(f: Float)
    private external fun nativePfSetAlpha(f: Float)
    private external fun nativePfSetDelta(f: Float)
    private external fun nativePfSetSigma(f: Float)
    private external fun nativePfSetLow(f: Float)
    private external fun nativePfSetMid(f: Float)
    private external fun nativePfSetHigh(f: Float)
    private external fun nativePfSetPresence(f: Float)

    // ── Native declarations — Reverb / Widener / Spatial ──────────────────
    private external fun nativeReverbSetType(type: String)
    private external fun nativeReverbSetDecay(f: Float)
    private external fun nativeReverbSetPreDelay(ms: Float)
    private external fun nativeReverbSetDamping(f: Float)
    private external fun nativeReverbSetDiffusion(f: Float)
    private external fun nativeReverbSetMix(f: Float)
    private external fun nativeReverbSetBypass(b: Boolean)
    private external fun nativeWiderSetWidth(f: Float)
    private external fun nativeWiderSetDepth(f: Float)
    private external fun nativeWiderSetMix(f: Float)
    private external fun nativeWiderSetDelay(ms: Float)
    private external fun nativeWiderSetBypass(b: Boolean)
    private external fun nativeSpatialSetWidth(f: Float)
    private external fun nativeSpatialSetMu(f: Float)
}
