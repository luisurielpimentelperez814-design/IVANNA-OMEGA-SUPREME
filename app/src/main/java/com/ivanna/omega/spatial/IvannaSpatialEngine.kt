package com.ivanna.omega.spatial

import kotlin.math.*

/**
 * IvannaSpatialEngine – Motor Binaural Espacial Kotlin Puro (V3).
 *
 * Reemplaza completamente al antiguo motor JNI. Implementa localización
 * binaural real (ITD+ILD), expansión estéreo M/S, early reflections con
 * absorción, crossfeed para audífonos y control de distancia perceptual.
 *
 * Compatible con la API original: companion object shared, enabled,
 * setAzimuth, setWidth, setHeadTracker (stub).
 */
class IvannaSpatialEngine private constructor() {

    // Parámetros ajustables (en el companion para acceso estático)
    companion object {
        val shared: IvannaSpatialEngine = IvannaSpatialEngine()

        /** Activa/desactiva el procesamiento. Usa fundido interno para suavidad. */
        @Volatile var enabled: Boolean = true

        /** Ángulo azimuth en radianes (-PI..PI). 0 = frente. */
        fun setAzimuth(rad: Float) { shared.azimuthRad = rad }

        /** Ancho estéreo. 0 = mono, 1 = original, 1.5 = extra ancho. */
        fun setWidth(v: Float) { shared.widthFactor = v.coerceIn(0f, 1.5f) }

        /** Distancia perceptual (0.5 = cerca, 2.0 = lejos). */
        fun setDistance(v: Float) { shared.distance = v.coerceIn(0.5f, 2.0f) }

        /** Stub para compatibilidad con AudioForegroundService. */
        fun setHeadTracker(yaw: Float, pitch: Float, roll: Float) { }
    }

    // Constantes de audio
    private val sampleRate = 44100
    private val maxBlockSize = 4096
    private val maxItdSamples = (0.0015f * sampleRate).toInt() // 1.5 ms
    private val crossfeedDelaySamples = 44                    // ~1 ms
    private val earlyReflectionTimes = intArrayOf(7, 13, 17, 23) // ms
    private val earlyReflectionGains = floatArrayOf(0.3f, 0.2f, 0.15f, 0.1f)

    // Estado interno
    @Volatile var azimuthRad: Float = 0f
    @Volatile var widthFactor: Float = 1.0f
    @Volatile var distance: Float = 1.0f

    // Fundido de enable/disable (evita clics)
    private var fadeGain = 1.0f          // 1 = totalmente activo, 0 = bypass
    private var targetEnabled = true     // hacia dónde transiciona
    private val fadeSpeed = 0.01f        // por muestra

    // Buffers pre‑allocados (sin GC en el hot path)
    private var tempMid = FloatArray(maxBlockSize)
    private var tempSide = FloatArray(maxBlockSize)
    private var delayedL = FloatArray(maxItdSamples)
    private var delayedR = FloatArray(maxItdSamples)
    private var earlyReflectionBuffers = Array(4) { FloatArray(maxBlockSize) }
    private var crossfeedBufferL = FloatArray(crossfeedDelaySamples)
    private var crossfeedBufferR = FloatArray(crossfeedDelaySamples)

    private var itdWriteIndex = 0
    private var crossfeedWriteIndex = 0
    private var earlyWriteIndices = IntArray(4)
    private var modPhase = 0f
    private var erLowpassState = FloatArray(4)

    fun init(modelPath: String? = null) { reset() }

    fun reset() {
        delayedL.fill(0f); delayedR.fill(0f)
        crossfeedBufferL.fill(0f); crossfeedBufferR.fill(0f)
        for (b in earlyReflectionBuffers) b.fill(0f)
        earlyWriteIndices.fill(0)
        erLowpassState.fill(0f)
        modPhase = 0f
        itdWriteIndex = 0; crossfeedWriteIndex = 0
        fadeGain = 1.0f; targetEnabled = true
    }

    fun release() { reset() }

    /**
     * Procesa un bloque de audio estéreo aplicando espacialización binaural.
     * Si enabled == false, la señal pasa sin cambios (con fundido).
     */
    fun processStereoInput(
        inL: FloatArray, inR: FloatArray,
        outL: FloatArray, outR: FloatArray,
        numFrames: Int
    ) {
        // Actualizar estado de fundido
        val desired = companion.enabled
        if (desired != targetEnabled) {
            targetEnabled = desired
        }
        // Transicionar fadeGain suavemente
        if (targetEnabled && fadeGain < 1f) {
            fadeGain = (fadeGain + fadeSpeed).coerceAtMost(1f)
        } else if (!targetEnabled && fadeGain > 0f) {
            fadeGain = (fadeGain - fadeSpeed).coerceAtLeast(0f)
        }

        // Si el procesamiento está completamente desactivado, copia directa
        if (fadeGain <= 0f) {
            System.arraycopy(inL, 0, outL, 0, numFrames)
            System.arraycopy(inR, 0, outR, 0, numFrames)
            return
        }

        val n = minOf(numFrames, maxBlockSize)
        val azimuth = azimuthRad; val width = widthFactor; val dist = distance
        val itdMax = maxItdSamples
        val cosAzimuth = cos(azimuth); val sinAzimuth = sin(azimuth)
        val modAmount = if (abs(azimuth) > 0.1f) sin(modPhase) * 0.5f else 0f
        modPhase += 0.0003f

        for (i in 0 until n) {
            // --- BINAURAL ITD + ILD ---
            val itdSamples = (sinAzimuth * itdMax).toInt()
            val modOffset = (modAmount * 1.5f).toInt()
            val totalDelayL = maxOf(0, -itdSamples + modOffset)
            val totalDelayR = maxOf(0, itdSamples + modOffset)

            val readIdxL = (itdWriteIndex - totalDelayL + itdMax) % itdMax
            val readIdxR = (itdWriteIndex - totalDelayR + itdMax) % itdMax
            val delayedSampleL = delayedL[readIdxL]
            val delayedSampleR = delayedR[readIdxR]

            val ildL = sqrt((1f + cosAzimuth) / 2f).coerceIn(0.3f, 1f)
            val ildR = sqrt((1f - cosAzimuth) / 2f).coerceIn(0.3f, 1f)
            var sampleL = inL[i] * ildL
            var sampleR = inR[i] * ildR

            delayedL[itdWriteIndex] = sampleL
            delayedR[itdWriteIndex] = sampleR
            sampleL = delayedSampleL; sampleR = delayedSampleR
            itdWriteIndex = (itdWriteIndex + 1) % itdMax

            // --- WIDENER M/S ---
            val mid = (sampleL + sampleR) * 0.5f
            val side = (sampleL - sampleR) * 0.5f
            val widenedSide = side * width.coerceIn(0f, 1.5f)
            val focusGain = if (width < 0.3f) 0.7f else 1f
            sampleL = mid + widenedSide; sampleR = mid - widenedSide

            // --- EARLY REFLECTIONS (con absorción) ---
            var erL = 0f; var erR = 0f
            for (j in 0 until 4) {
                val delaySamples = (earlyReflectionTimes[j] * sampleRate / 1000f).toInt() % maxBlockSize
                val readIdx = (earlyWriteIndices[j] - delaySamples + maxBlockSize) % maxBlockSize
                var reflected = earlyReflectionBuffers[j][readIdx] * earlyReflectionGains[j]
                erLowpassState[j] += 0.3f * (reflected - erLowpassState[j])
                reflected = erLowpassState[j]
                erL += reflected * 0.7f; erR += reflected * 0.7f
            }

            // --- CROSSFEED PARA AUDÍFONOS ---
            val xfeedFactor = 0.15f / dist
            val xfeedReadIdx = (crossfeedWriteIndex - crossfeedDelaySamples + crossfeedDelaySamples) % crossfeedDelaySamples
            val crossL = crossfeedBufferR[xfeedReadIdx] * xfeedFactor * focusGain
            val crossR = crossfeedBufferL[xfeedReadIdx] * xfeedFactor * focusGain

            // --- MEZCLA FINAL CON DISTANCIA Y FUNDIDO ---
            val distGain = 1f / sqrt(dist)
            var outSampleL = ((sampleL + erL + crossL) * distGain)
            var outSampleR = ((sampleR + erR + crossR) * distGain)

            // Aplicar fundido (bypass suave hacia señal original)
            outSampleL = outSampleL * fadeGain + inL[i] * (1f - fadeGain)
            outSampleR = outSampleR * fadeGain + inR[i] * (1f - fadeGain)

            outL[i] = outSampleL.coerceIn(-1f, 1f)
            outR[i] = outSampleR.coerceIn(-1f, 1f)

            // Actualizar buffers de reflexiones y crossfeed
            for (j in 0 until 4) {
                earlyReflectionBuffers[j][earlyWriteIndices[j]] = sampleL + sampleR
                earlyWriteIndices[j] = (earlyWriteIndices[j] + 1) % maxBlockSize
            }
            crossfeedBufferL[crossfeedWriteIndex] = sampleL
            crossfeedBufferR[crossfeedWriteIndex] = sampleR
            crossfeedWriteIndex = (crossfeedWriteIndex + 1) % crossfeedDelaySamples
        }

        // Rellenar con silencio si numFrames < maxBlockSize
        if (numFrames < n) {
            for (i in numFrames until n) {
                outL[i] = 0f; outR[i] = 0f
            }
        }
    }
}
