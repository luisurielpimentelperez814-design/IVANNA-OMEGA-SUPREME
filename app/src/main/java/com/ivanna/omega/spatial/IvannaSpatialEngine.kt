package com.ivanna.omega.spatial

import kotlin.math.*
import com.ivanna.omega.core.IvannaNativeLib

class IvannaSpatialEngine private constructor() {

    companion object {
        val shared: IvannaSpatialEngine = IvannaSpatialEngine()

        @Volatile var enabled: Boolean = true

        fun setAzimuth(rad: Float) { shared.azimuthRad = rad }
        fun setWidth(v: Float) { shared.widthFactor = v.coerceIn(0f, 1.5f) }
        fun setDistance(v: Float) { shared.distance = v.coerceIn(0.5f, 2.0f) }

        /**
         * Cablea el HeadTracker al motor nativo.
         * onOrientationChanged → nativeSetSpatialAngleRad (yaw) en el hilo
         * del sensor — zero-Compose, latencia mínima (~10 ms).
         */
        fun setHeadTracker(tracker: IvannaHeadTracker) =
            shared.attachHeadTracker(tracker)
    }

    private val sampleRate = 44100
    private val maxBlockSize = 4096
    private val maxItdSamples = (0.0015f * sampleRate).toInt()
    private val crossfeedDelaySamples = 44
    private val earlyReflectionTimes = intArrayOf(7, 13, 17, 23)
    private val earlyReflectionGains = floatArrayOf(0.3f, 0.2f, 0.15f, 0.1f)

    @Volatile var azimuthRad: Float = 0f
    @Volatile var widthFactor: Float = 1.0f
    @Volatile var distance: Float = 1.0f

    private var fadeGain = 1.0f
    private var targetEnabled = true
    private val fadeSpeed = 0.01f

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

    fun attachHeadTracker(tracker: IvannaHeadTracker) {
        tracker.onOrientationChanged = { pitch, yaw, roll ->
            if (IvannaNativeLib.isLoaded)
                IvannaNativeLib.nativeSetSpatialAngleRad(yaw)
            azimuthRad = yaw
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
        fadeGain = 1.0f; targetEnabled = true
    }

    fun release() { reset() }

    fun processStereoInput(
        inL: FloatArray, inR: FloatArray,
        outL: FloatArray, outR: FloatArray,
        numFrames: Int
    ) {
        val desired = IvannaSpatialEngine.enabled
        if (desired != targetEnabled) targetEnabled = desired
        if (targetEnabled && fadeGain < 1f) fadeGain = (fadeGain + fadeSpeed).coerceAtMost(1f)
        else if (!targetEnabled && fadeGain > 0f) fadeGain = (fadeGain - fadeSpeed).coerceAtLeast(0f)

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

            val mid = (sampleL + sampleR) * 0.5f
            val side = (sampleL - sampleR) * 0.5f
            val widenedSide = side * width.coerceIn(0f, 1.5f)
            val focusGain = if (width < 0.3f) 0.7f else 1f
            sampleL = mid + widenedSide; sampleR = mid - widenedSide

            var erL = 0f; var erR = 0f
            for (j in 0 until 4) {
                val delaySamples = (earlyReflectionTimes[j] * sampleRate / 1000f).toInt() % maxBlockSize
                val readIdx = (earlyWriteIndices[j] - delaySamples + maxBlockSize) % maxBlockSize
                var reflected = earlyReflectionBuffers[j][readIdx] * earlyReflectionGains[j]
                erLowpassState[j] += 0.3f * (reflected - erLowpassState[j])
                reflected = erLowpassState[j]
                erL += reflected * 0.7f; erR += reflected * 0.7f
            }

            val xfeedFactor = 0.15f / dist
            val xfeedReadIdx = (crossfeedWriteIndex - crossfeedDelaySamples + crossfeedDelaySamples) % crossfeedDelaySamples
            val crossL = crossfeedBufferR[xfeedReadIdx] * xfeedFactor * focusGain
            val crossR = crossfeedBufferL[xfeedReadIdx] * xfeedFactor * focusGain

            val distGain = 1f / sqrt(dist)
            var outSampleL = ((sampleL + erL + crossL) * distGain)
            var outSampleR = ((sampleR + erR + crossR) * distGain)

            outSampleL = outSampleL * fadeGain + inL[i] * (1f - fadeGain)
            outSampleR = outSampleR * fadeGain + inR[i] * (1f - fadeGain)

            outL[i] = outSampleL.coerceIn(-1f, 1f)
            outR[i] = outSampleR.coerceIn(-1f, 1f)

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
