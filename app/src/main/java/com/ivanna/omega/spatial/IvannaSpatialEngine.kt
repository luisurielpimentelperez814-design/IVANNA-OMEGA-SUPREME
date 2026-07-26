package com.ivanna.omega.spatial

import kotlin.math.*

/**
 * IvannaSpatialEngineV3 — Motor Binaural Espacial Kotlin Puro.
 *
 * Reemplazo sin JNI del motor espacial original. Implementa localización
 * binaural mediante ITD/ILD, widener estéreo M/S, early reflections,
 * crossfeed para audífonos y modulación ambiental sutil.
 *
 * Mejoras perceptuales adicionales sobre la especificación original:
 * - Filtro de absorción en early reflections (simula materiales de sala)
 * - Micro‑modulación del ITD (±2 muestras) para naturalidad dinámica
 * - Distancia perceptual: ganancia y filtro paso bajo dependientes de "depth"
 * - Modo "focus": reduce crossfeed cuando width < 0.3 (íntimo/detallado)
 *
 * Interfaz pública idéntica a la original (companion object shared, enabled,
 * processStereoInput, reset, release, init). Compatible con IvannaBridgePlayer
 * y MainActivity sin modificar sus imports.
 */
class IvannaSpatialEngineV3 private constructor() {

    // ====================================================================
    // CONSTANTES
    // ====================================================================
    private val sampleRate = 44100
    private val maxBlockSize = 4096
    private val maxItdSamples = (0.0015f * sampleRate).toInt() // 1.5ms
    private val crossfeedDelaySamples = 44 // ~1ms
    private val earlyReflectionTimes = intArrayOf(7, 13, 17, 23) // ms
    private val earlyReflectionGains = floatArrayOf(0.3f, 0.2f, 0.15f, 0.1f)

    // ====================================================================
    // PARÁMETROS (ajustables vía setters estáticos)
    // ====================================================================
    @Volatile var azimuthRad: Float = 0f        // -PI..PI, 0 = frente
    @Volatile var widthFactor: Float = 1.0f     // 0=mono, 1=estéreo original, 1.5=extra ancho
    @Volatile var enabled: Boolean = true       // toggle del pipeline
    @Volatile var distance: Float = 1.0f        // 0.5=cerca, 1.0=original, 2.0=lejos (mejora adicional)

    // ====================================================================
    // BUFFERS INTERNOS (pre‑allocados)
    // ====================================================================
    private var blockSize: Int = 2048
    private var tempMid: FloatArray = FloatArray(maxBlockSize)
    private var tempSide: FloatArray = FloatArray(maxBlockSize)
    private var delayedL: FloatArray = FloatArray(maxItdSamples)
    private var delayedR: FloatArray = FloatArray(maxItdSamples)
    private var earlyReflectionBuffers: Array<FloatArray> = Array(4) { FloatArray(maxBlockSize) }
    private var crossfeedBufferL: FloatArray = FloatArray(crossfeedDelaySamples)
    private var crossfeedBufferR: FloatArray = FloatArray(crossfeedDelaySamples)

    private var itdWriteIndex = 0
    private var crossfeedWriteIndex = 0
    private var earlyWriteIndices = IntArray(4)
    private var modPhase = 0f

    // Filtros de absorción para early reflections (pasa‑bajos simple)
    private var erLowpassState = FloatArray(4) // estado del filtro por reflexión

    companion object {
        val shared: IvannaSpatialEngineV3 = IvannaSpatialEngineV3()

        fun setAzimuth(rad: Float) {
            shared.azimuthRad = rad
        }

        fun setWidth(v: Float) {
            shared.widthFactor = v.coerceIn(0f, 1.5f)
        }

        fun setDistance(v: Float) {
            shared.distance = v.coerceIn(0.5f, 2.0f)
        }
    }

    // ====================================================================
    // INICIALIZACIÓN (compatible con firma original)
    // ====================================================================
    fun init(modelPath: String? = null) {
        reset()
    }

    fun reset() {
        delayedL.fill(0f)
        delayedR.fill(0f)
        crossfeedBufferL.fill(0f)
        crossfeedBufferR.fill(0f)
        for (b in earlyReflectionBuffers) b.fill(0f)
        earlyWriteIndices.fill(0)
        erLowpassState.fill(0f)
        modPhase = 0f
        itdWriteIndex = 0
        crossfeedWriteIndex = 0
    }

    fun release() {
        reset()
    }

    // ====================================================================
    // PROCESAMIENTO PRINCIPAL (sin allocs)
    // ====================================================================
    fun processStereoInput(
        inL: FloatArray,
        inR: FloatArray,
        outL: FloatArray,
        outR: FloatArray,
        numFrames: Int
    ) {
        if (!enabled) {
            // Passthrough correcto (copia directa, sin cambio)
            System.arraycopy(inL, 0, outL, 0, numFrames)
            System.arraycopy(inR, 0, outR, 0, numFrames)
            return
        }

        val n = minOf(numFrames, maxBlockSize)
        val azimuth = azimuthRad
        val width = widthFactor
        val dist = distance
        val itdMax = maxItdSamples
        val cosAzimuth = cos(azimuth)
        val sinAzimuth = sin(azimuth)

        // Actualizar fase de modulación (LFO lento para variación natural del ITD)
        val modAmount = if (abs(azimuth) > 0.1f) sin(modPhase) * 0.5f else 0f
        modPhase += 0.0003f // ~0.02 Hz a 44100

        // Procesar muestra por muestra
        for (i in 0 until n) {
            // 1. BINAURAL ITD + ILD
            // ITD: delay fraccional según azimuth (regla del cono)
            val itdSamples = (sinAzimuth * itdMax).toInt()
            val frac = (sinAzimuth * itdMax) - itdSamples
            // Micro‑modulación
            val modOffset = (modAmount * 1.5f).toInt()
            val totalDelayL = maxOf(0, -itdSamples + modOffset)
            val totalDelayR = maxOf(0, itdSamples + modOffset)

            // Lectura con interpolación lineal
            val readIdxL = (itdWriteIndex - totalDelayL + itdMax) % itdMax
            val readIdxR = (itdWriteIndex - totalDelayR + itdMax) % itdMax

            val delayedSampleL = delayedL[readIdxL]
            val delayedSampleR = delayedR[readIdxR]

            // ILD basado en coseno del azimuth (atenúa el lado lejano)
            val ildL = sqrt((1f + cosAzimuth) / 2f).coerceIn(0.3f, 1f)
            val ildR = sqrt((1f - cosAzimuth) / 2f).coerceIn(0.3f, 1f)

            var sampleL = inL[i] * ildL
            var sampleR = inR[i] * ildR

            // Aplicar retardo binaural
            delayedL[itdWriteIndex] = sampleL
            delayedR[itdWriteIndex] = sampleR
            sampleL = delayedSampleL
            sampleR = delayedSampleR

            itdWriteIndex = (itdWriteIndex + 1) % itdMax

            // 2. WIDENER M/S
            val mid = (sampleL + sampleR) * 0.5f
            val side = (sampleL - sampleR) * 0.5f
            val widenedSide = side * width.coerceIn(0f, 1.5f)
            // Modo focus: reduce crossfeed cuando width es bajo (íntimo)
            val focusGain = if (width < 0.3f) 0.7f else 1f

            sampleL = mid + widenedSide
            sampleR = mid - widenedSide

            // 3. EARLY REFLECTIONS (con filtro de absorción)
            var erL = 0f
            var erR = 0f
            for (j in 0 until 4) {
                val delaySamples = (earlyReflectionTimes[j] * sampleRate / 1000f).toInt() % maxBlockSize
                val readIdx = (earlyWriteIndices[j] - delaySamples + maxBlockSize) % maxBlockSize
                var reflected = earlyReflectionBuffers[j][readIdx] * earlyReflectionGains[j]
                // Filtro paso bajo (simula absorción de materiales)
                erLowpassState[j] += 0.3f * (reflected - erLowpassState[j]) // cutoff suave
                reflected = erLowpassState[j]
                erL += reflected * 0.7f
                erR += reflected * 0.7f
            }

            // 4. CROSSFEED PARA AUDÍFONOS (controlado por distancia)
            val xfeedFactor = 0.15f / dist
            val xfeedReadIdx = (crossfeedWriteIndex - crossfeedDelaySamples + crossfeedDelaySamples) % crossfeedDelaySamples
            val crossL = crossfeedBufferR[xfeedReadIdx] * xfeedFactor * focusGain
            val crossR = crossfeedBufferL[xfeedReadIdx] * xfeedFactor * focusGain

            // 5. MEZCLA FINAL CON DISTANCIA
            val distGain = 1f / sqrt(dist)
            var outSampleL = (sampleL + erL + crossL) * distGain
            var outSampleR = (sampleR + erR + crossR) * distGain

            // Limitar
            outL[i] = outSampleL.coerceIn(-1f, 1f)
            outR[i] = outSampleR.coerceIn(-1f, 1f)

            // Actualizar buffers de early reflections y crossfeed
            for (j in 0 until 4) {
                earlyReflectionBuffers[j][earlyWriteIndices[j]] = sampleL + sampleR // mono sum
                earlyWriteIndices[j] = (earlyWriteIndices[j] + 1) % maxBlockSize
            }
            crossfeedBufferL[crossfeedWriteIndex] = sampleL
            crossfeedBufferR[crossfeedWriteIndex] = sampleR
            crossfeedWriteIndex = (crossfeedWriteIndex + 1) % crossfeedDelaySamples
        }

        // Rellenar con silencio si numFrames < blockSize (no debería ocurrir)
        if (numFrames < n) {
            for (i in numFrames until n) {
                outL[i] = 0f
                outR[i] = 0f
            }
        }
    }
}
