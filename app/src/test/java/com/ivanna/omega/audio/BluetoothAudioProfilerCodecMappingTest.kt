package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

class BluetoothAudioProfilerCodecMappingTest {
    @Test fun opusAndUnknownNeverClaimTransparentProcessingFromCodecIdAlone() {
        assertEquals(false, BluetoothAudioProfiler.isTransparentCodec(BluetoothAudioProfiler.Codec.OPUS))
        assertEquals(false, BluetoothAudioProfiler.isTransparentCodec(BluetoothAudioProfiler.Codec.UNKNOWN))
        assertEquals(false, BluetoothAudioProfiler.isTransparentCodec(BluetoothAudioProfiler.Codec.SBC))
        assertEquals(true, BluetoothAudioProfiler.isTransparentCodec(BluetoothAudioProfiler.Codec.APTX_HD))
        assertEquals(true, BluetoothAudioProfiler.isTransparentCodec(BluetoothAudioProfiler.Codec.LDAC))
        assertEquals(true, BluetoothAudioProfiler.isTransparentCodec(BluetoothAudioProfiler.Codec.WIRED))
    }

    @Test fun mapsStableA2dpCodecIds() {
        assertEquals(BluetoothAudioProfiler.Codec.SBC, BluetoothAudioProfiler.codecFromInt(0, 35))
        assertEquals(BluetoothAudioProfiler.Codec.AAC, BluetoothAudioProfiler.codecFromInt(1, 35))
        assertEquals(BluetoothAudioProfiler.Codec.APTX, BluetoothAudioProfiler.codecFromInt(2, 35))
        assertEquals(BluetoothAudioProfiler.Codec.APTX_HD, BluetoothAudioProfiler.codecFromInt(3, 35))
        assertEquals(BluetoothAudioProfiler.Codec.LDAC, BluetoothAudioProfiler.codecFromInt(4, 35))
    }

    @Test fun unknownOrVendorSpecificIdsNeverBecomeAnUnrelatedCodec() {
        assertEquals(BluetoothAudioProfiler.Codec.UNKNOWN, BluetoothAudioProfiler.codecFromInt(5, 35))
        assertEquals(BluetoothAudioProfiler.Codec.UNKNOWN, BluetoothAudioProfiler.codecFromInt(99, 35))
        assertEquals(BluetoothAudioProfiler.Codec.UNKNOWN, BluetoothAudioProfiler.codecFromInt(Int.MAX_VALUE, 35))
        assertEquals(BluetoothAudioProfiler.Codec.UNKNOWN, BluetoothAudioProfiler.codecFromInt(-1, 35))
        assertEquals(BluetoothAudioProfiler.Codec.UNKNOWN, BluetoothAudioProfiler.codecFromInt(6, 32))
    }

    @Test fun opusIdIsOnlyRecognizedOnAndroid13AndNewer() {
        assertEquals(BluetoothAudioProfiler.Codec.OPUS, BluetoothAudioProfiler.codecFromInt(6, 33))
        assertEquals(BluetoothAudioProfiler.Codec.OPUS, BluetoothAudioProfiler.codecFromInt(6, 35))
    }
}
