package com.ivanna.omega.audio

import kotlin.math.abs

/**
 * Fail-closed drift estimator for the app-owned AudioTrack path.
 * Uses frame-position/timestamp pairs from one getTimestamp() call; it does not
 * write mixer controls, modify vendor HAL, or control other apps' output clocks.
 */
internal class AudioTrackClockDriftServo(private val nominalSampleRateHz: Int) {
    companion object {
        private const val MIN_INTERVAL_NS = 500_000_000L
        private const val MAX_INTERVAL_NS = 2_000_000_000L
        private const val MAX_DRIFT_PPM = 500.0
        private const val REJECT_DRIFT_PPM = 5_000.0
        private const val MAX_SLEW_PPM_PER_UPDATE = 25.0
        private const val LOCK_SAMPLES = 3
    }

    private var previousFramePosition = -1L
    private var previousTimestampNs = 0L
    private var validSamples = 0
    var correctionPpm: Double = 0.0
        private set

    fun observe(framePosition: Long, timestampNs: Long): Double {
        if (nominalSampleRateHz !in 8_000..768_000 ||
            framePosition < 0L || timestampNs <= 0L) {
            reset()
            return 0.0
        }
        if (previousFramePosition < 0L) {
            seed(framePosition, timestampNs)
            return 0.0
        }
        if (framePosition < previousFramePosition || timestampNs < previousTimestampNs) {
            reset()
            seed(framePosition, timestampNs)
            return 0.0
        }
        // Do not erase the measurement window when the playback head is stationary.
        if (framePosition == previousFramePosition || timestampNs == previousTimestampNs) {
            return correctionPpm
        }

        val elapsedNs = timestampNs - previousTimestampNs
        if (elapsedNs < MIN_INTERVAL_NS) return correctionPpm
        if (elapsedNs > MAX_INTERVAL_NS) {
            reset()
            seed(framePosition, timestampNs)
            return 0.0
        }

        val frameDelta = framePosition - previousFramePosition
        seed(framePosition, timestampNs)
        if (frameDelta <= 0L || frameDelta > nominalSampleRateHz.toLong() * 2L) {
            reset()
            seed(framePosition, timestampNs)
            return 0.0
        }
        val measuredRate = frameDelta.toDouble() * 1_000_000_000.0 / elapsedNs.toDouble()
        val driftPpm = (measuredRate / nominalSampleRateHz.toDouble() - 1.0) * 1_000_000.0
        if (!measuredRate.isFinite() || !driftPpm.isFinite() ||
            measuredRate < nominalSampleRateHz * 0.5 ||
            measuredRate > nominalSampleRateHz * 1.5 ||
            abs(driftPpm) >= REJECT_DRIFT_PPM) {
            reset()
            seed(framePosition, timestampNs)
            return 0.0
        }

        if (validSamples < LOCK_SAMPLES) {
            validSamples++
            if (validSamples < LOCK_SAMPLES) return 0.0
        }
        val target = driftPpm.coerceIn(-MAX_DRIFT_PPM, MAX_DRIFT_PPM)
        val filtered = correctionPpm + 0.2 * (target - correctionPpm)
        correctionPpm += (filtered - correctionPpm).coerceIn(
            -MAX_SLEW_PPM_PER_UPDATE, MAX_SLEW_PPM_PER_UPDATE
        )
        return correctionPpm
    }

    fun reset() {
        previousFramePosition = -1L
        previousTimestampNs = 0L
        validSamples = 0
        correctionPpm = 0.0
    }

    private fun seed(framePosition: Long, timestampNs: Long) {
        previousFramePosition = framePosition
        previousTimestampNs = timestampNs
    }
}
