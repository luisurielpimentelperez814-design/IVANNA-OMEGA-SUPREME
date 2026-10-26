package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

class BluetoothCodecDetectorExtendedMappingTest {
    @Test fun ambiguousVendorIdFiveNeverClaimsAptxAdaptive() {
        assertEquals(BluetoothCodecDetector.RealCodec.UNKNOWN, BluetoothCodecDetector.mapExtendedCodec(5, 35))
        assertEquals(BluetoothCodecDetector.RealCodec.UNKNOWN, BluetoothCodecDetector.mapExtendedCodec(5, 32))
    }

    @Test fun opusIdSixIsRecognizedOnlyOnAndroid13OrNewer() {
        assertEquals(BluetoothCodecDetector.RealCodec.UNKNOWN, BluetoothCodecDetector.mapExtendedCodec(6, 32))
        assertEquals(BluetoothCodecDetector.RealCodec.OPUS, BluetoothCodecDetector.mapExtendedCodec(6, 33))
        assertEquals(BluetoothCodecDetector.RealCodec.OPUS, BluetoothCodecDetector.mapExtendedCodec(6, 35))
    }

    @Test fun allOtherExtendedIdsFailClosed() {
        for (id in listOf(-1, 7, 99, Int.MAX_VALUE)) {
            assertEquals("codec id $id", BluetoothCodecDetector.RealCodec.UNKNOWN,
                BluetoothCodecDetector.mapExtendedCodec(id, 35))
        }
    }
}
