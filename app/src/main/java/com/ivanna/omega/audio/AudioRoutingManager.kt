package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build

object AudioRoutingManager {
    // FIX (bug real de restoreDefaultRouting): antes forzaba speakerphone=true
    // y bluetoothA2dp=true incondicionalmente, encendiéndolos aunque el
    // usuario no los tuviera activos antes de forceUsbDacRouting(). Ahora se
    // guarda el estado previo real y se restaura ese, no un valor fijo.
    private var wasSpeakerphoneOn: Boolean = false
    private var wasBluetoothA2dpOn: Boolean = false

    fun forceUsbDacRouting(context: Context, audioTrack: AudioTrack? = null): Boolean {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            for (device in devices) {
                if (device.type == AudioDeviceInfo.TYPE_USB_DEVICE || 
                    device.type == AudioDeviceInfo.TYPE_USB_HEADSET) {
                    wasSpeakerphoneOn = audioManager.isSpeakerphoneOn
                    wasBluetoothA2dpOn = audioManager.isBluetoothA2dpOn
                    audioManager.isSpeakerphoneOn = false
                    audioManager.isBluetoothA2dpOn = false
                    audioTrack?.preferredDevice = device
                    return true
                }
            }
        }
        return false
    }

    fun restoreDefaultRouting(context: Context, audioTrack: AudioTrack? = null): Boolean {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        audioManager.isSpeakerphoneOn = wasSpeakerphoneOn
        audioManager.isBluetoothA2dpOn = wasBluetoothA2dpOn
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            audioTrack?.preferredDevice = null
        }
        return true
    }
}
