package com.ivanna.omega.sensing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class CsiFeatureExtractorTest {
    @Before
    fun reset() {
        CsiFeatureExtractor.reset()
    }

    @Test
    fun computesAmplitudeStatisticsFromActualCsiSamples() {
        val result = CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(
                timestampNanos = 100L,
                subcarrierAmplitudes = floatArrayOf(1f, 2f, 3f, 4f),
                subcarrierPhases = floatArrayOf(0f, 0f, 0f, 0f),
                antennaCount = 2,
                sampleRateHz = 100
            )
        )
        assertEquals(4, result.sampleCount)
        assertEquals(2.5, result.meanAmplitude, 0.0001)
        assertEquals(1.1180, result.amplitudeStdDev, 0.001)
        assertNotNull(result.phaseCoherence)
        assertTrue(result.quality in 0.0..1.0)
        assertNull(result.temporalAmplitudeDelta)
    }

    @Test
    fun computesTemporalDeltaBetweenCompatibleFrames() {
        val first = UniversalWifiSensingAdapter.CsiSample(1L, floatArrayOf(1f, 1f), null, 1, 10)
        val second = UniversalWifiSensingAdapter.CsiSample(2L, floatArrayOf(2f, 2f), null, 1, 10)
        CsiFeatureExtractor.analyze(first)
        val result = CsiFeatureExtractor.analyze(second)
        assertNotNull(result.temporalAmplitudeDelta)
        assertTrue(result.temporalAmplitudeDelta!! > 0.0)
    }

    @Test
    fun rejectsEmptyOrInvalidAmplitudeFrames() {
        val result = CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(1L, floatArrayOf(Float.NaN, -1f), null, 0, 0)
        )
        assertEquals(0, result.sampleCount)
        assertEquals(0.0, result.quality, 0.0)
        assertTrue(result.warnings.isNotEmpty())
    }

    @Test
    fun rejectsPartiallyInvalidAmplitudeFrameInsteadOfShiftingSubcarrierIndices() {
        val result = CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(
                timestampNanos = 10L,
                subcarrierAmplitudes = floatArrayOf(1f, Float.NaN, 3f),
                subcarrierPhases = null,
                antennaCount = 1,
                sampleRateHz = 100
            )
        )
        assertEquals(0, result.sampleCount)
        assertEquals(0.0, result.quality, 0.0)
        assertTrue(result.warnings.any { it.contains("descartado") })
    }

    @Test
    fun doesNotComputeTemporalDeltaAcrossIncompatibleSampleRates() {
        CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(10L, floatArrayOf(1f, 1f), null, 1, 10)
        )
        val result = CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(20L, floatArrayOf(2f, 2f), null, 1, 20)
        )
        assertNull(result.temporalAmplitudeDelta)
    }

    @Test
    fun ignoresOutOfOrderTimestampForTemporalDelta() {
        CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(20L, floatArrayOf(1f, 1f), null, 1, 10)
        )
        val result = CsiFeatureExtractor.analyze(
            UniversalWifiSensingAdapter.CsiSample(10L, floatArrayOf(2f, 2f), null, 1, 10)
        )
        assertNull(result.temporalAmplitudeDelta)
        assertTrue(result.warnings.any { it.contains("fuera de orden") })
    }
}
