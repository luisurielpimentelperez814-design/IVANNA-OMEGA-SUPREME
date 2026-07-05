package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.util.Log

/**
 * AudioRouteManager — detecta la ruta de salida de audio activa
 * (Bluetooth A2DP / AUX cableado / USB-C / altavoz interno) y aplica
 * un perfil de compensación real vía AudioEngine.nativeSetRouteProfile(),
 * que se funde en el pipeline DSP a través de UnifiedControlFrame
 * (ver audio_control_plane.hpp: control_set_route_profile()).
 *
 * Motivación: los codecs Bluetooth lossy (SBC/AAC) colapsan la banda
 * 2-4kHz y el estéreo se degrada al recodificar; el AUX cableado sufre
 * rolloff de graves por impedancia de salida en muchos dispositivos
 * Android de gama media. Ninguna de las dos rutas se corrige sola.
 */
enum class OutputRoute { BLUETOOTH, WIRED_AUX, USB, SPEAKER, UNKNOWN }

data class RouteProfile(
    val bassBoostDb: Float,
    val dialogBoostDb: Float,
    val widenerMult: Float
)

object AudioRouteManager {
    private const val TAG = "AudioRouteManager"

    private var audioManager: AudioManager? = null
    private var deviceCallback: AudioDeviceCallback? = null
    private var currentRoute: OutputRoute = OutputRoute.UNKNOWN

    fun start(context: Context) {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        audioManager = am

        applyRoute(detectOutputRoute(am))

        val callback = object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
                applyRoute(detectOutputRoute(am))
            }
            override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
                applyRoute(detectOutputRoute(am))
            }
        }
        am.registerAudioDeviceCallback(callback, null)
        deviceCallback = callback
    }

    fun stop() {
        val am = audioManager ?: return
        deviceCallback?.let { am.unregisterAudioDeviceCallback(it) }
        deviceCallback = null
        audioManager = null
    }

    fun detectOutputRoute(am: AudioManager = audioManager ?: return OutputRoute.UNKNOWN): OutputRoute {
        val outputs = am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        return when {
            outputs.any { it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP || it.type == AudioDeviceInfo.TYPE_BLE_HEADSET } ->
                OutputRoute.BLUETOOTH
            outputs.any {
                it.type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES || it.type == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
                it.type == AudioDeviceInfo.TYPE_AUX_LINE
            } -> OutputRoute.WIRED_AUX
            outputs.any {
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET || it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                it.type == AudioDeviceInfo.TYPE_USB_ACCESSORY
            } -> OutputRoute.USB
            outputs.any { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER } -> OutputRoute.SPEAKER
            else -> OutputRoute.UNKNOWN
        }
    }

    fun profileFor(route: OutputRoute): RouteProfile = when (route) {
        // SBC/AAC pierden presencia 2-4kHz y el estéreo se degrada al
        // recodificar; se compensa diálogo y se reduce ancho.
        OutputRoute.BLUETOOTH -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 3.5f, widenerMult = 0.65f)
        // AUX cableado: rolloff de graves común por impedancia de salida.
        OutputRoute.WIRED_AUX -> RouteProfile(bassBoostDb = 2.0f, dialogBoostDb = 1.0f, widenerMult = 1.0f)
        // USB-C: DAC dedicado, sin compensación necesaria.
        OutputRoute.USB -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 0.5f, widenerMult = 1.0f)
        OutputRoute.SPEAKER, OutputRoute.UNKNOWN -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 0f, widenerMult = 1.0f)
    }

    private fun applyRoute(route: OutputRoute) {
        if (route == currentRoute) return
        currentRoute = route
        val p = profileFor(route)
        Log.i(TAG, "Ruta de salida: $route -> bassBoost=${p.bassBoostDb}dB dialogBoost=${p.dialogBoostDb}dB widenerMult=${p.widenerMult}")
        AudioEngine.nativeSetRouteProfile(p.bassBoostDb, p.dialogBoostDb, p.widenerMult)
    }
}
