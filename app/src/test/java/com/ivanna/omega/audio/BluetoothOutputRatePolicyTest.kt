package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

class BluetoothOutputRatePolicyTest {
    @Test fun selectsSupportedNegotiatedRates() {
        assertEquals(44_100, BluetoothOutputRatePolicy.select(44_100))
        assertEquals(48_000, BluetoothOutputRatePolicy.select(48_000))
        assertEquals(88_200, BluetoothOutputRatePolicy.select(88_200))
        assertEquals(96_000, BluetoothOutputRatePolicy.select(96_000))
    }

    @Test fun unknownOrUnsupportedRatesFallBackTo48Khz() {
        assertEquals(48_000, BluetoothOutputRatePolicy.select(0))
        assertEquals(48_000, BluetoothOutputRatePolicy.select(32_000))
        assertEquals(48_000, BluetoothOutputRatePolicy.select(176_400))
        assertEquals(48_000, BluetoothOutputRatePolicy.select(-1))
    }
}
