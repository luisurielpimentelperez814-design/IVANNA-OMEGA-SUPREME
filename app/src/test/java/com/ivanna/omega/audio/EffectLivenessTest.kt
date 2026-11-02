package com.ivanna.omega.audio

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EffectLivenessTest {
    @Test fun successIsAlive() = assertTrue(EffectLiveness.isAlive(0))
    @Test fun deadObjectIsNotAlive() = assertFalse(EffectLiveness.isAlive(EffectLiveness.DEAD))
    @Test fun anyErrorStatusIsNotAlive() {
        for (c in intArrayOf(-1, -2, -3, -4, -5, -19, -32)) assertFalse(EffectLiveness.isAlive(c))
    }
    @Test fun nullProbeIsNotAlive() = assertFalse(EffectLiveness.isAlive(null))
}
