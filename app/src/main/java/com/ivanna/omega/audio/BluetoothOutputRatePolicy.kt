package com.ivanna.omega.audio

/**
 * Chooses a conservative AudioTrack rate for an app-owned Bluetooth media path.
 *
 * A2DP's negotiated codec rate is preferred when Android reports a recognized
 * active rate. The Android Bluetooth stack still owns encoder/transport clocks;
 * this policy does not claim to servo the remote sink or alter vendor HAL.
 */
internal object BluetoothOutputRatePolicy {
    const val FALLBACK_RATE_HZ = 48_000

    fun select(codecRateHz: Int): Int = when (codecRateHz) {
        44_100, 48_000, 88_200, 96_000 -> codecRateHz
        else -> FALLBACK_RATE_HZ
    }
}
