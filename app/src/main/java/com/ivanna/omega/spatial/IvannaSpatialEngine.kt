package com.ivanna.omega.spatial

import kotlin.math.*

/**
 * IvannaSpatialEngine – Motor Binaural Espacial Kotlin Puro (V3).
 * Mantiene la misma API pública que el motor JNI original.
 */
class IvannaSpatialEngine private constructor() {

    // Constantes
    private val sampleRate = 44100
    private val maxBlockSize = 4096
    private val maxItdSamples = (0.0015f * sampleRate).toInt()
    private val crossfeedDelaySamples = 44
    private val earlyReflectionTimes = intArrayOf(7, 13, 17, 23)
    private val earlyReflectionGains = floatArrayOf(0.3f, 0.2f, 0.15f, 0.1f)

    // Parámetros ajustables
    @Volatile var azimuthRad: Float = 0f
    @Volatile var widthFactor: Float = 1.0f
    @Volatile var enabled: Boolean = true
    @Volatile var distance: Float = 1.0f

    // Buffers pre‑allocados
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
    private var erLowpassState = FloatArray(4)

    companion object {
        val shared: IvannaSpatialEngine = IvannaSpatialEngine()

        fun setAzimuth(rad: Float) { shared.azimuthRad = rad }
        fun setWidth(v: Float) { shared.widthFactor = v.coerceIn(0f, 1.5f) }
        fun setDistance(v: Float) { shared.distance = v.coerceIn(0.5f, 2.0f) }
        fun setHeadTracker(yaw: Float, pitch: Float, roll: Float) {
            // Stub: el motor espacial Kotlin no usa head tracker
        }
    }

    fun init(modelPath: String? = null) { reset() }
    fun reset() {
        delayedL.fill(0f); delayedR.fill(0f)
        crossfeedBufferL.fill(0f); crossfeedBufferR.fill(0f)
        earlyReflectionBuffers.forEach { it.fill(0f) }
        earlyWriteIndices.fill(0)
        erLowpassState.fill(0f)
        modPhase = 0f
        itdWriteIndex = 0; crossfeedWriteIndex = 0
    }
    fun release() { reset() }

    fun processStereoInput(
        inL: FloatArray, inR: FloatArray,
        outL: FloatArray, outR: FloatArray,
        numFrames: Int
    ) {
        if (!enabled) {
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

            // Widener M/S
            val mid = (sampleL + sampleR) * 0.5f
            val side = (sampleL - sampleR) * 0.5f
            val widenedSide = side * width.coerceIn(0f, 1.5f)
            val focusGain = if (width < 0.3f) 0.7f else 1f
            sampleL = mid + widenedSide; sampleR = mid - widenedSide

            // Early reflections
            var erL = 0f; var erR = 0f
            for (j in 0 until 4) {
                val delaySamples = (earlyReflectionTimes[j] * sampleRate / 1000f).toInt() % maxBlockSize
                val readIdx = (earlyWriteIndices[j] - delaySamples + maxBlockSize) % maxBlockSize
                var reflected = earlyReflectionBuffers[j][readIdx] * earlyReflectionGains[j]
                erLowpassState[j] += 0.3f * (reflected - erLowpassState[j])
                reflected = erLowpassState[j]
                erL += reflected * 0.7f; erR += reflected * 0.7f
            }

            // Crossfeed
            val xfeedFactor = 0.15f / dist
            val xfeedReadIdx = (crossfeedWriteIndex - crossfeedDelaySamples + crossfeedDelaySamples) % crossfeedDelaySamples
            val crossL = crossfeedBufferR[xfeedReadIdx] * xfeedFactor * focusGain
            val crossR = crossfeedBufferL[xfeedReadIdx] * xfeedFactor * focusGain

            // Mezcla final
            val distGain = 1f / sqrt(dist)
            outL[i] = ((sampleL + erL + crossL) * distGain).coerceIn(-1f, 1f)
            outR[i] = ((sampleR + erR + crossR) * distGain).coerceIn(-1f, 1f)

            // Actualizar buffers
            for (j in 0 until 4) {
                earlyReflectionBuffers[j][earlyWriteIndices[j]] = sampleL + sampleR
                earlyWriteIndices[j] = (earlyWriteIndices[j] + 1) % maxBlockSize
            }
            crossfeedBufferL[crossfeedWriteIndex] = sampleL
            crossfeedBufferR[crossfeedWriteIndex] = sampleR
            crossfeedWriteIndex = (crossfeedWriteIndex + 1) % crossfeedDelaySamples
        }
        if (numFrames < n) {
            for (i in numFrames until n) { outL[i] = 0f; outR[i] = 0f }
        }
    }
}
