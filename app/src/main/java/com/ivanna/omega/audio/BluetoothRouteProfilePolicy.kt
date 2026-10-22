package com.ivanna.omega.audio

/**
 * Deterministic codec-to-DSP policy. Codec identity is evidence about transport,
 * not a measurement of the connected headphone's frequency response.
 * Keep profiles bounded and conservative; never synthesize missing codec data.
 */
object BluetoothRouteProfilePolicy {
    fun fallback(bitrateKbps: Int? = null): RouteProfile =
        if (bitrateKbps != null && bitrateKbps in 1 until 200) {
            RouteProfile(bassBoostDb = 0f, dialogBoostDb = 4.5f, widenerMult = 0.5f)
        } else {
            RouteProfile(bassBoostDb = 0f, dialogBoostDb = 3.5f, widenerMult = 0.65f)
        }

    fun forCodec(codec: BluetoothCodecDetector.RealCodec): RouteProfile = when (codec) {
        BluetoothCodecDetector.RealCodec.SBC ->
            RouteProfile(0f, 4.5f, 0.5f)
        BluetoothCodecDetector.RealCodec.AAC ->
            RouteProfile(0f, 3.0f, 0.7f)
        BluetoothCodecDetector.RealCodec.APTX ->
            RouteProfile(0f, 1.5f, 0.85f)
        BluetoothCodecDetector.RealCodec.APTX_HD,
        BluetoothCodecDetector.RealCodec.APTX_ADAPTIVE ->
            RouteProfile(0f, 0.5f, 0.95f)
        BluetoothCodecDetector.RealCodec.LDAC ->
            RouteProfile(0f, 0f, 1f)
        BluetoothCodecDetector.RealCodec.OPUS ->
            RouteProfile(0f, 1f, 0.9f)
        BluetoothCodecDetector.RealCodec.UNKNOWN,
        BluetoothCodecDetector.RealCodec.UNAVAILABLE -> fallback()
    }
}
