package com.ivanna.omega.audio

import kotlin.math.sqrt
import org.junit.Assert.*
import org.junit.Test

class EffectTuningPolicyTest {
    @Test fun mapsFiveHardwareBandsToTheirActualFrequencies() {
        val curve = intArrayOf(-1000, -900, -800, -700, -600, -500, -400, -300, -200, -100)
        val result = EffectTuningPolicy.deviceBands(curve, intArrayOf(63_000, 250_000, 1_000_000, 4_000_000, 16_000_000), -1500, 1500)
        assertArrayEquals(intArrayOf(-900, -700, -500, -300, -100), result)
    }
    @Test fun interpolatesInLogFrequencyAndHandlesNonstandardBandCounts() {
        val curve = intArrayOf(-1000, -900, -800, -700, -600, -500, -400, -300, -200, -100)
        val midpoint = (sqrt(1000.0 * 2000.0) * 1000).toInt()
        assertArrayEquals(intArrayOf(-1000, -450, -100),
            EffectTuningPolicy.deviceBands(curve, intArrayOf(20_000, midpoint, 24_000_000), -1500, 1500))
    }
    @Test fun combinedVideoIsoAndManualBoostsShareOneHeadroomCalculation() {
        val state = EffectTuningPolicy.State(isoMb = List(10) { 400 },
            live = EffectTuningPolicy.live(3f, 1.3f, -12f, 2f), safSigma = 1f)
        val curve = EffectTuningPolicy.curve(IntArray(10) { 200 }, state, true)
        assertEquals(1270, curve[5])
        val mapped = EffectTuningPolicy.deviceBands(curve,
            intArrayOf(31_000, 250_000, 1_000_000, 4_000_000, 16_000_000), -600, 600)
        assertEquals(0, mapped[2])
        assertTrue(mapped.all { it in -600..0 })
    }
    @Test fun repeatedSafAndIsoCompositionDoesNotAccumulateOrMutateBase() {
        val base = intArrayOf(200, 160, 100, 40, 0, 0, 80, 160, 200, 160)
        val original = base.copyOf()
        val manual = EffectTuningPolicy.live(-2f, 1.4f, -10f, 2f)
        val state = EffectTuningPolicy.State(isoMb = EffectTuningPolicy.iso(FloatArray(10) { 1f }), live = manual, safSigma = 0.8f)
        val target = EffectTuningPolicy.curve(base, state, false)
        repeat(100) { assertArrayEquals(target, EffectTuningPolicy.curve(base, state, false)) }
        assertArrayEquals(original, base)
        assertEquals(-28, target[4])
        assertEquals(manual, state.copy(safSigma = 0.2f).live)
    }
    @Test fun widthRangeRemainsControllableAndExternalSpeakersDisableVirtualization() {
        val strengths = listOf(0f, .5f, 1f, 1.5f, 2f).map {
            EffectTuningPolicy.virtualizer(460, EffectTuningPolicy.State(live = EffectTuningPolicy.live(0f, it, -12f, 2f)), true).toInt()
        }
        assertEquals(listOf(0, 55, 110, 165, 220), strengths)
        assertEquals(0, EffectTuningPolicy.virtualizer(460, EffectTuningPolicy.State(safSigma = 1f), false).toInt())
    }
    @Test fun malformedControlsHaveFiniteBoundedDefaultsAndHardwareLimits() {
        val live = EffectTuningPolicy.live(Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.NaN)
        assertEquals(0f, live.eqGainDb, 0f)
        assertEquals(1f, live.width, 0f)
        assertTrue(live.thresholdDb.isFinite() && live.ratio.isFinite())
        assertEquals(0, EffectTuningPolicy.iso(floatArrayOf(Float.NaN))[0])
        assertEquals(1200, EffectTuningPolicy.iso(floatArrayOf(100f))[0])
        val curve = EffectTuningPolicy.curve(intArrayOf(Int.MAX_VALUE), EffectTuningPolicy.State(isoMb = listOf(Int.MAX_VALUE)), false)
        assertEquals(1500, curve[0])
        assertTrue(EffectTuningPolicy.deviceBands(curve, intArrayOf(31_000, 63_000, 0), -300, 300).all { it in -300..0 })
    }
    @Test fun safetyReliefAndBassChoiceSurviveSpatialUpdates() {
        val state = EffectTuningPolicy.State(bassStrength = 280, loudnessReliefMb = 100)
        for (sigma in listOf(0f, .5f, 1f)) {
            val changed = state.copy(safSigma = sigma)
            assertEquals(280, EffectTuningPolicy.bass(150, changed).toInt())
            assertEquals(50, EffectTuningPolicy.loudness(300, changed, true))
            assertEquals(0, EffectTuningPolicy.loudness(300, changed, false))
        }
        assertEquals(300, EffectTuningPolicy.bass(1000, EffectTuningPolicy.State()).toInt())
    }
    @Test fun allNamedProfilesRemainBoundedAcrossVideoAndDeviceBandLayouts() {
        for ((name, profile) in IvannaEffectProfile.byName) for (video in listOf(false, true)) {
            val curve = EffectTuningPolicy.curve(profile.eqBands, EffectTuningPolicy.State(), video)
            val actual = EffectTuningPolicy.deviceBands(curve,
                intArrayOf(60_000, 230_000, 910_000, 3_600_000, 14_000_000), -1200, 1200)
            assertTrue(name, actual.all { it in -1200..0 })
        }
    }
    @Test fun omegaBandsKeepTheCurveShapeWithoutAnyPositiveBand() {
        val curve = intArrayOf(300, 200, 100, 0, -100, 0, 150, 250, 300, 100)
        val db = EffectTuningPolicy.omegaBandsDb(curve)
        assertEquals(10, db.size)
        assertTrue(db.all { it <= 0f && it >= -12f })
        assertEquals(0f, db.maxOrNull()!!, 0.001f)               // el boost mayor queda en 0 dB
        assertEquals(-4f, db[4], 0.001f)                  // -100 mB - 300 mB de headroom
        assertEquals(db[0] - db[2], 2f, 0.001f)           // la forma relativa se conserva
    }
    @Test fun omegaBandsOfFlatOrCutOnlyCurveAreUntouched() {
        assertArrayEquals(FloatArray(10), EffectTuningPolicy.omegaBandsDb(IntArray(10)), 0.0001f)
        val cut = IntArray(10) { -200 }
        assertEquals(-2f, EffectTuningPolicy.omegaBandsDb(cut)[3], 0.0001f)
    }
}
