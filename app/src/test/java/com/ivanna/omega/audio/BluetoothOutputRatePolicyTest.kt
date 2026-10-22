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

    @Test fun codecLookupRequiresAnUnambiguousSelectedDeviceAddress() {
        assertEquals(true, BluetoothOutputRatePolicy.hasReliableDeviceAddress("AA:BB:CC:DD:EE:FF"))
        assertEquals(false, BluetoothOutputRatePolicy.hasReliableDeviceAddress(null))
        assertEquals(false, BluetoothOutputRatePolicy.hasReliableDeviceAddress(""))
        assertEquals(false, BluetoothOutputRatePolicy.hasReliableDeviceAddress("   "))
    }
    @Test fun codecLookupNeverBorrowsAnotherOrAnonymousDevice() {
        assertEquals(true, BluetoothOutputRatePolicy.matchesSelectedDevice("AA:BB:CC:DD:EE:FF", "aa:bb:cc:dd:ee:ff"))
        assertEquals(false, BluetoothOutputRatePolicy.matchesSelectedDevice(null, "AA:BB:CC:DD:EE:FF"))
        assertEquals(false, BluetoothOutputRatePolicy.matchesSelectedDevice("   ", "AA:BB:CC:DD:EE:FF"))
        assertEquals(false, BluetoothOutputRatePolicy.matchesSelectedDevice("AA:BB:CC:DD:EE:FF", null))
        assertEquals(false, BluetoothOutputRatePolicy.matchesSelectedDevice("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"))
    }

}
