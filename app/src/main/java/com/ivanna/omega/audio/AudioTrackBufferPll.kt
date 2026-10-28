package com.ivanna.omega.audio

import kotlin.math.abs

/**
 * SAFMUS clock servo for exactly one app-owned AudioTrack.
 *
 * One controller combines a rolling least-squares estimate of the presented
 * frame clock with a bounded buffer-occupancy PI term. Positive drift means
 * the AudioTrack consumer clock runs faster than nominal, so positive ppm
 * asks StereoAudioResampler to produce output frames faster.
 *
 * This is software rate correction for IVANNA's own track only. It does not
 * discipline a physical DAC, Bluetooth sink, vendor HAL, or another app.
 */
internal class AudioTrackBufferPll(private val nominalSampleRateHz: Int) {
    companion object {
        private const val MIN_INTERVAL_NS = 10_000_000L
        private const val MAX_INTERVAL_NS = 2_000_000_000L
        private const val MAX_CORRECTION_PPM = 500.0
        private const val MAX_SLEW_PPM_PER_UPDATE = 25.0
        private const val KP_PPM_PER_FULL_BUFFER = 800.0
        private const val KI_PPM_PER_FULL_BUFFER_SECOND = 120.0
        private const val MAX_INTEGRAL_SECONDS = 4.0
        private const val LOCK_SAMPLES = 5
        private const val REGRESSION_WINDOW = 8
        private const val REQUIRED_REGRESSION_POINTS = 4
        private const val MAX_MEASURED_DRIFT_PPM = 2_000.0
        private const val DRIFT_FILTER_ALPHA = 0.2
    }

    private var previousTimestampNs = 0L
    private var previousConsumedFrames = -1L
    private var integralErrorSeconds = 0.0
    private var validSamples = 0
    private val frameHistory = LongArray(REGRESSION_WINDOW)
    private val timeHistoryNs = LongArray(REGRESSION_WINDOW)
    private var historyCount = 0
    private var historyNext = 0
    private var filteredDriftPpm = 0.0
    private var hasDriftEstimate = false

    var correctionPpm: Double = 0.0
        private set
    var occupancyFrames: Long = 0L
        private set
    var targetOccupancyFrames: Long = 0L
        private set
    var phaseErrorFraction: Double = 0.0
        private set
    var measuredRateHz: Double = 0.0
        private set
    var measuredDriftPpm: Double = 0.0
        private set

    val isLocked: Boolean get() = validSamples >= LOCK_SAMPLES
    val isDriftLocked: Boolean get() = historyCount >= REQUIRED_REGRESSION_POINTS && hasDriftEstimate

    /**
     * @param submittedFrames cumulative stereo frames accepted by this AudioTrack
     * @param consumedFrames cumulative presented frames from this same track's AudioTimestamp
     * @param bufferCapacityFrames configured AudioTrack buffer size in frames
     * @param timestampNs monotonic timestamp paired with consumedFrames
     * @return bounded ppm correction for StereoAudioResampler
     */
    fun observe(
        submittedFrames: Long,
        consumedFrames: Long,
        bufferCapacityFrames: Int,
        timestampNs: Long
    ): Double {
        if (nominalSampleRateHz !in 8_000..768_000 ||
            submittedFrames < 0L || consumedFrames < 0L ||
            bufferCapacityFrames <= 0 || timestampNs <= 0L) {
            reset()
            return 0.0
        }

        val queued = submittedFrames - consumedFrames
        val capacity = bufferCapacityFrames.toLong()
        if (queued < 0L || queued > capacity * 2L) {
            reset()
            return 0.0
        }

        occupancyFrames = queued
        targetOccupancyFrames = (capacity / 2L).coerceAtLeast(1L)
        phaseErrorFraction = ((queued - targetOccupancyFrames).toDouble() / capacity)
            .coerceIn(-1.0, 1.0)

        if (previousTimestampNs == 0L) {
            previousTimestampNs = timestampNs
            previousConsumedFrames = consumedFrames
            addClockSample(consumedFrames, timestampNs)
            return 0.0
        }

        if (timestampNs <= previousTimestampNs || consumedFrames < previousConsumedFrames) {
            reset()
            previousTimestampNs = timestampNs
            previousConsumedFrames = consumedFrames
            addClockSample(consumedFrames, timestampNs)
            return 0.0
        }

        val elapsedNs = timestampNs - previousTimestampNs
        if (elapsedNs < MIN_INTERVAL_NS) return correctionPpm
        if (elapsedNs > MAX_INTERVAL_NS) {
            reset()
            previousTimestampNs = timestampNs
            previousConsumedFrames = consumedFrames
            addClockSample(consumedFrames, timestampNs)
            return 0.0
        }

        val dtSeconds = elapsedNs / 1_000_000_000.0
        integralErrorSeconds = (integralErrorSeconds + phaseErrorFraction * dtSeconds)
            .coerceIn(-MAX_INTEGRAL_SECONDS, MAX_INTEGRAL_SECONDS)

        previousTimestampNs = timestampNs
        previousConsumedFrames = consumedFrames
        addClockSample(consumedFrames, timestampNs)
        estimateClockDrift()

        // Buffer PI corrects queue phase; regression feed-forward compensates
        // the measured output-clock rate. Both terms share ONE actuator/limit.
        val driftFeedForward = if (isDriftLocked) filteredDriftPpm else 0.0
        val occupancyCorrection = -(KP_PPM_PER_FULL_BUFFER * phaseErrorFraction +
            KI_PPM_PER_FULL_BUFFER_SECOND * integralErrorSeconds)
        val target = (driftFeedForward + occupancyCorrection)
            .coerceIn(-MAX_CORRECTION_PPM, MAX_CORRECTION_PPM)

        if (validSamples < LOCK_SAMPLES) validSamples++
        val delta = (target - correctionPpm)
            .coerceIn(-MAX_SLEW_PPM_PER_UPDATE, MAX_SLEW_PPM_PER_UPDATE)
        correctionPpm = (correctionPpm + delta)
            .coerceIn(-MAX_CORRECTION_PPM, MAX_CORRECTION_PPM)
        return correctionPpm
    }

    private fun addClockSample(framePosition: Long, timestampNs: Long) {
        if (historyCount > 0) {
            val previousIndex = if (historyNext == 0) REGRESSION_WINDOW - 1 else historyNext - 1
            if (timestampNs <= timeHistoryNs[previousIndex] ||
                framePosition <= frameHistory[previousIndex]) return
        }
        frameHistory[historyNext] = framePosition
        timeHistoryNs[historyNext] = timestampNs
        historyNext = (historyNext + 1) % REGRESSION_WINDOW
        if (historyCount < REGRESSION_WINDOW) historyCount++
    }

    /** Centered OLS on paired frame/timestamp samples avoids large-epoch precision loss. */
    private fun estimateClockDrift() {
        if (historyCount < REQUIRED_REGRESSION_POINTS) return
        val oldest = if (historyCount == REGRESSION_WINDOW) historyNext else 0
        val t0 = timeHistoryNs[oldest]
        val f0 = frameHistory[oldest]
        var meanT = 0.0
        var meanF = 0.0
        for (i in 0 until historyCount) {
            val idx = (oldest + i) % REGRESSION_WINDOW
            meanT += (timeHistoryNs[idx] - t0).toDouble() * 1.0e-9
            meanF += (frameHistory[idx] - f0).toDouble()
        }
        meanT /= historyCount
        meanF /= historyCount
        var covariance = 0.0
        var variance = 0.0
        for (i in 0 until historyCount) {
            val idx = (oldest + i) % REGRESSION_WINDOW
            val t = (timeHistoryNs[idx] - t0).toDouble() * 1.0e-9 - meanT
            val f = (frameHistory[idx] - f0).toDouble() - meanF
            covariance += t * f
            variance += t * t
        }
        if (variance <= 1.0e-12 || !variance.isFinite()) return
        val rate = covariance / variance
        val drift = (rate / nominalSampleRateHz.toDouble() - 1.0) * 1_000_000.0
        if (!rate.isFinite() || !drift.isFinite() ||
            rate < nominalSampleRateHz * 0.5 ||
            rate > nominalSampleRateHz * 1.5 ||
            abs(drift) > MAX_MEASURED_DRIFT_PPM) {
            return // retain last valid estimate; never turn one outlier into control
        }
        measuredRateHz = rate
        measuredDriftPpm = drift
        filteredDriftPpm = if (hasDriftEstimate) {
            filteredDriftPpm + DRIFT_FILTER_ALPHA * (drift - filteredDriftPpm)
        } else {
            drift
        }
        hasDriftEstimate = true
    }

    fun reset() {
        previousTimestampNs = 0L
        previousConsumedFrames = -1L
        integralErrorSeconds = 0.0
        validSamples = 0
        historyCount = 0
        historyNext = 0
        filteredDriftPpm = 0.0
        hasDriftEstimate = false
        correctionPpm = 0.0
        occupancyFrames = 0L
        targetOccupancyFrames = 0L
        phaseErrorFraction = 0.0
        measuredRateHz = 0.0
        measuredDriftPpm = 0.0
        frameHistory.fill(0L)
        timeHistoryNs.fill(0L)
    }
}
