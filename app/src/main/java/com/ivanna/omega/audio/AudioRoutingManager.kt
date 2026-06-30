package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build

object AudioRoutingManager {
    fun forceUsbDacRouting(context: Context, audioTrack: AudioTrack? = null): Boolean {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            for (device in devices) {
                if (device.type == AudioDeviceInfo.TYPE_USB_DEVICE || 
                    device.type == AudioDeviceInfo.TYPE_USB_HEADSET) {
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
        audioManager.isSpeakerphoneOn = true
        audioManager.isBluetoothA2dpOn = true
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            audioTrack?.preferredDevice = null
        }
        return true
    }
}
