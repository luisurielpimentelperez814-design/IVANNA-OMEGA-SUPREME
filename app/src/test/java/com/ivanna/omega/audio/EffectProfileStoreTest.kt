package com.ivanna.omega.audio

import org.junit.Assert.*
import org.junit.Test

class EffectProfileStoreTest {
    @Test fun customAndBuiltInProfilesRoundTripWithoutChangingParameters() {
        val custom = IvannaEffectProfile.PUNCH.copy(eqBands = intArrayOf(90, 80, 70, 60, 50, 40, 30, 20, 10, -10), compRatio = 1.8f)
        for (profile in IvannaEffectProfile.byName.values + custom) {
            val restored = EffectProfileStore.decode(EffectProfileStore.encode(profile))!!
            assertArrayEquals(profile.eqBands, restored.eqBands)
            assertEquals(profile, restored.copy(eqBands = profile.eqBands))
        }
    }
    @Test fun corruptedOrNonfiniteSnapshotsAreRejected() {
        assertNull(EffectProfileStore.decode("{truncated"))
        assertNull(EffectProfileStore.decode("{\"compRatio\":NaN}"))
        assertNull(EffectProfileStore.decode("{\"eqBands\":\"wrong type\"}"))
    }
}
