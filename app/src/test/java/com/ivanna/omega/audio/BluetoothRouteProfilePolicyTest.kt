package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

class BluetoothRouteProfilePolicyTest {
    @Test fun lossyAndHighFidelityCodecsReceiveDistinctBoundedProfiles() {
        val sbc = BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.SBC)
        val aac = BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.AAC)
        val aptx = BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.APTX)
        val aptxHd = BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.APTX_HD)
        val ldac = BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.LDAC)

        assertEquals(4.5f, sbc.dialogBoostDb, 0f)
        assertEquals(0.5f, sbc.widenerMult, 0f)
        assertEquals(3f, aac.dialogBoostDb, 0f)
        assertEquals(1.5f, aptx.dialogBoostDb, 0f)
        assertEquals(0.5f, aptxHd.dialogBoostDb, 0f)
        assertEquals(0f, ldac.dialogBoostDb, 0f)
        assertEquals(1f, ldac.widenerMult, 0f)
    }

    @Test fun unknownCodecDoesNotPretendCodecDetectionSucceeded() {
        assertEquals(
            BluetoothRouteProfilePolicy.fallback(),
            BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.UNKNOWN)
        )
        assertEquals(
            BluetoothRouteProfilePolicy.fallback(),
            BluetoothRouteProfilePolicy.forCodec(BluetoothCodecDetector.RealCodec.UNAVAILABLE)
        )
    }

    @Test fun vendorBitrateFallbackUsesOnlyPositiveReportedValues() {
        assertEquals(4.5f, BluetoothRouteProfilePolicy.fallback(199).dialogBoostDb, 0f)
        assertEquals(3.5f, BluetoothRouteProfilePolicy.fallback(200).dialogBoostDb, 0f)
        assertEquals(3.5f, BluetoothRouteProfilePolicy.fallback(null).dialogBoostDb, 0f)
        assertEquals(3.5f, BluetoothRouteProfilePolicy.fallback(0).dialogBoostDb, 0f)
        assertEquals(0f, BluetoothRouteProfilePolicy.fallback(-1).bassBoostDb, 0f)
    }
}
