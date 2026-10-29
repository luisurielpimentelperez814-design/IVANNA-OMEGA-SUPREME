package com.ivanna.omega.audio

import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AudioTrackBufferPllTest {
    @Test fun producerAheadReducesOutputRateWithBoundedSlew() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        assertEquals(0.0, pll.observe(8_000, 0, 10_000, timeNs), 0.0)
        repeat(8) {
            timeNs += 100_000_000L
            pll.observe(8_000, 0, 10_000, timeNs)
        }
        assertTrue("full queue must reduce output frame production", pll.correctionPpm < 0.0)
        assertTrue("correction must remain bounded", pll.correctionPpm in -500.0..500.0)
        assertTrue("integrator must not cause a sudden correction", pll.correctionPpm >= -200.0)
        assertTrue(pll.isLocked)
        assertEquals(8_000L, pll.occupancyFrames)
        assertEquals(5_000L, pll.targetOccupancyFrames)
    }

    @Test fun consumerAheadIncreasesOutputRate() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(2_000, 0, 10_000, timeNs)
        repeat(5) {
            timeNs += 100_000_000L
            pll.observe(2_000, 0, 10_000, timeNs)
        }
        assertTrue("low queue must increase output frame production", pll.correctionPpm > 0.0)
        assertTrue(pll.correctionPpm <= 125.0)
    }

    @Test fun smallBufferErrorInsideDeadbandDoesNotHunt() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        // Capacity 10,000; target 5,000; 5,100 is only 1% above target.
        var submitted = 5_100L
        var consumed = 0L
        pll.observe(submitted, consumed, 10_000, timeNs)
        repeat(20) {
            timeNs += 100_000_000L
            submitted += 4_800L
            consumed += 4_800L
            val correction = pll.observe(submitted, consumed, 10_000, timeNs)
            assertEquals("deadband suppresses small occupancy noise", 0.0, correction, 0.0)
        }
    }

    @Test fun sustainedSaturationRemainsBounded() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(9_000, 0, 10_000, timeNs)
        repeat(300) {
            timeNs += 100_000_000L
            assertTrue(pll.observe(9_000, 0, 10_000, timeNs) in -500.0..500.0)
        }
        assertEquals(-500.0, pll.correctionPpm, 0.0)
    }

    @Test fun invalidQueueAndTimestampFailClosed() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(8_000, 0, 10_000, 1_000_000_000L)
        pll.observe(8_000, 0, 10_000, 1_100_000_000L)
        assertTrue(pll.correctionPpm < 0.0)

        assertEquals(0.0, pll.observe(10, 20, 10_000, 1_200_000_000L), 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue(!pll.isLocked)
    }

    @Test fun rollingRegressionMeasuresPositiveConsumerClockDriftAndFeedsForward() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(20) {
            // 48,048 frames per second = +1,000 ppm against 48 kHz nominal.
            val submitted = consumed + 5_000L
            pll.observe(submitted, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue("regression must lock after paired frame/time samples", pll.isDriftLocked)
        assertEquals(48_048.0, pll.measuredRateHz, 0.5)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)
        assertTrue("faster consumer needs positive resampler production correction", pll.correctionPpm > 0.0)
        assertTrue(pll.correctionPpm <= 500.0)
    }

    @Test fun negativeConsumerClockDriftProducesNegativeResamplerCorrection() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(20) {
            // 47,952 frames/s = -1,000 ppm against 48 kHz nominal.
            val submitted = consumed + 5_000L
            pll.observe(submitted, consumed, 10_000, timeNs)
            consumed += 11_988L
            timeNs += 250_000_000L
        }
        assertTrue("regression must lock on a slower consumer clock", pll.isDriftLocked)
        assertEquals(47_952.0, pll.measuredRateHz, 0.5)
        assertEquals(-1_000.0, pll.measuredDriftPpm, 10.0)
        assertTrue("slower consumer must request negative resampler correction", pll.correctionPpm < 0.0)
        assertTrue(pll.correctionPpm >= -500.0)
    }

    @Test fun implausibleClockOutlierClearsStaleFeedForwardAndReacquires() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(8) {
            pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue("valid clock samples should establish drift lock", pll.isDriftLocked)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)

        // A single impossible frame/timestamp pair must not remain in the OLS
        // window or keep the previous clock feed-forward alive.
        consumed += 20_000L
        timeNs += 250_000_000L
        pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)

        assertTrue("outlier must drop clock lock until fresh samples arrive", !pll.isDriftLocked)
        assertEquals(0.0, pll.measuredDriftPpm, 0.0)
        assertEquals(0.0, pll.measuredRateHz, 0.0)
    }

    @Test fun frozenPresentedFramePositionClearsDriftEstimate() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(8) {
            pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue(pll.isDriftLocked)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)
        consumed -= 12_012L // reuse the last presented frame position
        timeNs += 250_000_000L
        val correction = pll.observe(consumed + 5_000L, consumed, 10_000, timeNs)
        assertTrue(!pll.isDriftLocked)
        assertEquals(0.0, pll.measuredDriftPpm, 0.0)
        assertEquals(0.0, pll.measuredRateHz, 0.0)
        assertTrue(correction in -500.0..500.0)
    }

    @Test fun timestampRegressionResetsClockAndActuatorToUnity() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(5_000, 0, 10_000, 2_000_000_000L)
        repeat(6) { i ->
            pll.observe(5_000 + (i + 1) * 12_000L, (i + 1) * 12_000L, 10_000,
                2_000_000_000L + (i + 1) * 250_000_000L)
        }
        assertEquals(0.0, pll.observe(1_000, 100, 10_000, 1_000_000_000L), 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue(!pll.isLocked)
    }

    @Test fun outputCorrectionAlwaysStaysWithinFiveHundredPpm() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(9_000, 0, 10_000, timeNs)
        repeat(200) {
            timeNs += 100_000_000L
            val value = pll.observe(9_000, 0, 10_000, timeNs)
            assertTrue(value in -500.0..500.0)
        }
        assertEquals(-500.0, pll.correctionPpm, 0.0)
    }

    // ── Jitter de timestamp REAL de la salida USB (AudioFlinger, captura Moto G85 + DAC USB full-speed) ──
    // sigma ~0.137 ms; picos de hasta 109 ms (dump: jitterMs std=0.137 max=109). Generador determinista.
    private class UsbTimestampModel(private val trueDriftPpm: Double, seed: Long) {
        private var state = seed
        private fun u(): Double { state = state * 6364136223846793005L + 1442695040888963407L
            return ((state ushr 11).toDouble() / (1L shl 53).toDouble()) }
        private fun gauss(): Double { var a = 0.0; repeat(12) { a += u() }; return a - 6.0 }
        fun jitterNs(): Long {
            var j = gauss() * 0.137e-3
            if (u() < 5.0e-4) j += 0.002 + u() * 0.107        // pico atipico positivo (2..109 ms)
            return (j * 1.0e9).toLong()
        }
        fun framesAt(tSeconds: Double): Long = (tSeconds * 48_000.0 * (1.0 + trueDriftPpm * 1.0e-6)).toLong()
    }

    private fun runUsbSession(trueDriftPpm: Double, polls: Int, seed: Long): Pair<DoubleArray, DoubleArray> {
        val pll = AudioTrackBufferPll(48_000)
        val model = UsbTimestampModel(trueDriftPpm, seed)
        val measured = DoubleArray(polls); val corr = DoubleArray(polls)
        for (k in 0 until polls) {
            val t = 1.0 + k * 0.25
            val consumed = model.framesAt(t)
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong() + model.jitterNs())
            measured[k] = pll.measuredDriftPpm; corr[k] = pll.correctionPpm
        }
        return measured to corr
    }

    @Test fun usbTimestampJitterAndSpikesDoNotCorruptDriftEstimate() {
        for ((trueDrift, seed) in listOf(10.0 to 1L, -18.0 to 2L, 0.0 to 3L)) {
            val (measured, corr) = runUsbSession(trueDrift, polls = 480, seed = seed)   // 120 s
            val tail = measured.copyOfRange(380, 480)
            val worst = tail.maxOf { abs(it - trueDrift) }
            assertTrue("deriva real $trueDrift ppm: error maximo de la estimacion $worst ppm (limite 12 ppm)", worst < 12.0)
            val corrWorst = corr.copyOfRange(380, 480).maxOf { abs(it - trueDrift) }
            assertTrue("la correccion debe seguir la deriva real $trueDrift ppm (error $corrWorst ppm, limite 15 ppm)", corrWorst < 15.0)
        }
    }

    @Test fun naiveEightPointOlsWouldFailTheSameJitterNegativeControl() {
        // Caso negativo: el estimador ANTERIOR (OLS de 8 puntos) sobre exactamente los mismos datos debe superar
        // el limite que el estimador actual cumple; demuestra que la prueba anterior detecta el defecto.
        val model = UsbTimestampModel(10.0, 1L)
        val tNs = LongArray(480); val f = LongArray(480)
        for (k in 0 until 480) { val t = 1.0 + k * 0.25; f[k] = model.framesAt(t); tNs[k] = (t * 1.0e9).toLong() + model.jitterNs() }
        var worst = 0.0
        for (end in 388..480) {
            val n = 8; var mt = 0.0; var mf = 0.0
            for (i in end - n until end) { mt += (tNs[i] - tNs[end - n]) * 1e-9; mf += (f[i] - f[end - n]).toDouble() }
            mt /= n; mf /= n
            var cov = 0.0; var v = 0.0
            for (i in end - n until end) { val a = (tNs[i] - tNs[end - n]) * 1e-9 - mt; val b = (f[i] - f[end - n]).toDouble() - mf; cov += a * b; v += a * a }
            worst = maxOf(worst, abs((cov / v / 48_000.0 - 1.0) * 1.0e6 - 10.0))
        }
        assertTrue("el OLS de 8 puntos debia fallar el limite de 12 ppm (peor error $worst ppm)", worst > 12.0)
    }

    @Test fun singleHugeTimestampSpikeDoesNotMoveTheEstimate() {
        val pll = AudioTrackBufferPll(48_000)
        for (k in 0 until 200) {
            val t = 1.0 + k * 0.25
            val consumed = (t * 48_000.0 * (1.0 + 10.0e-6)).toLong()
            val spike = if (k == 150) 109_000_000L else 0L
            pll.observe(consumed + 5_000L, consumed, 10_000, (t * 1.0e9).toLong() + spike)
        }
        assertEquals(10.0, pll.measuredDriftPpm, 3.0)
    }
}
