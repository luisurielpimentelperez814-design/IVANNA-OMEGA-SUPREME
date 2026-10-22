package com.ivanna.omega.audio

import kotlin.math.abs

/**
 * Software phase-locked loop for IVANNA's app-owned AudioTrack queue.
 *
 * Phase error is estimated from frames submitted to AudioTrack minus frames
 * consumed according to AudioTimestamp. The PI loop adjusts only the app's
 * resampler ratio; it never attempts to change a Bluetooth, DAC, or vendor-HAL
 * hardware clock. One instance belongs to one playback track and one writer.
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
    }

    private var previousTimestampNs = 0L
    private var integralErrorSeconds = 0.0
    private var validSamples = 0

    var correctionPpm: Double = 0.0
        private set
    var occupancyFrames: Long = 0L
        private set
    var targetOccupancyFrames: Long = 0L
        private set
    var phaseErrorFraction: Double = 0.0
        private set

    val isLocked: Boolean get() = validSamples >= LOCK_SAMPLES

    /**
     * @param submittedFrames cumulative stereo frames accepted by AudioTrack
     * @param consumedFrames cumulative playback-head frames from AudioTimestamp
     * @param bufferCapacityFrames configured AudioTrack buffer size in frames
     * @param timestampNs monotonic timestamp associated with consumedFrames
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
        // A negative or wildly oversized queue estimate usually means the
        // timestamp/head has not aligned with this track yet. Fail closed.
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
            return 0.0
        }
        val elapsedNs = timestampNs - previousTimestampNs
        if (elapsedNs <= 0L) {
            reset()
            return 0.0
        }
        if (elapsedNs < MIN_INTERVAL_NS) return correctionPpm
        if (elapsedNs > MAX_INTERVAL_NS) {
            reset()
            previousTimestampNs = timestampNs
            return 0.0
        }
        previousTimestampNs = timestampNs

        val dtSeconds = elapsedNs / 1_000_000_000.0
        integralErrorSeconds = (integralErrorSeconds + phaseErrorFraction * dtSeconds)
            .coerceIn(-MAX_INTEGRAL_SECONDS, MAX_INTEGRAL_SECONDS)

        // Positive queue error means the producer is running ahead; lower the
        // resampler output rate. StereoAudioResampler's positive ppm increases
        // output frames, hence the negative PI polarity here.
        val target = (-(KP_PPM_PER_FULL_BUFFER * phaseErrorFraction +
            KI_PPM_PER_FULL_BUFFER_SECOND * integralErrorSeconds))
            .coerceIn(-MAX_CORRECTION_PPM, MAX_CORRECTION_PPM)

        if (validSamples < LOCK_SAMPLES) validSamples++
        val delta = (target - correctionPpm)
            .coerceIn(-MAX_SLEW_PPM_PER_UPDATE, MAX_SLEW_PPM_PER_UPDATE)
        correctionPpm = (correctionPpm + delta).coerceIn(-MAX_CORRECTION_PPM, MAX_CORRECTION_PPM)
        return correctionPpm
    }

    fun reset() {
        previousTimestampNs = 0L
        integralErrorSeconds = 0.0
        validSamples = 0
        correctionPpm = 0.0
        occupancyFrames = 0L
        targetOccupancyFrames = 0L
        phaseErrorFraction = 0.0
    }
}
