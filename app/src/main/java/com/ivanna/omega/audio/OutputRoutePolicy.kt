package com.ivanna.omega.audio

/**
 * Deterministic precedence when Android reports several connected output endpoints.
 * USB wins over stale connected Bluetooth devices so a plugged DAC is not
 * accidentally assigned the Bluetooth profile. This is connection precedence,
 * not proof of AudioPolicy's currently routed endpoint.
 */
internal object OutputRoutePolicy {
    fun select(
        usbConnected: Boolean,
        bluetoothConnected: Boolean,
        wiredConnected: Boolean,
        speakerConnected: Boolean,
    ): OutputRoute = when {
        usbConnected -> OutputRoute.USB
        bluetoothConnected -> OutputRoute.BLUETOOTH
        wiredConnected -> OutputRoute.WIRED_AUX
        speakerConnected -> OutputRoute.SPEAKER
        else -> OutputRoute.UNKNOWN
    }
}
