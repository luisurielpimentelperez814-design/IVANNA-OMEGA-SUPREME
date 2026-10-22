package com.ivanna.omega.audio

/** Conservative sample-rate policy for Ivanna-owned A2DP playback only. */
internal object BluetoothOutputRatePolicy {
    const val FALLBACK_RATE_HZ = 48_000

    fun select(codecRateHz: Int): Int = when (codecRateHz) {
        44_100, 48_000, 88_200, 96_000 -> codecRateHz
        else -> FALLBACK_RATE_HZ
    }

    /** Codec metadata may only be borrowed from the selected output when its address is known. */
    fun hasReliableDeviceAddress(address: String?): Boolean = !address.isNullOrBlank()

    /** Never infer a codec from another connected headset when route identity is missing. */
    fun matchesSelectedDevice(selectedAddress: String?, candidateAddress: String?): Boolean =
        hasReliableDeviceAddress(selectedAddress) && hasReliableDeviceAddress(candidateAddress) &&
            selectedAddress!!.trim().equals(candidateAddress!!.trim(), ignoreCase = true)
}
