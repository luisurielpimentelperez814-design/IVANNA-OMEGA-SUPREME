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

    fun detectOutputRoute(): OutputRoute {
        val am = audioManager ?: return OutputRoute.UNKNOWN
        return detectOutputRoute(am)
    }

    fun detectOutputRoute(am: AudioManager): OutputRoute {
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
        OutputRoute.BLUETOOTH -> btProfile()
        // AUX cableado: rolloff de graves común por impedancia de salida.
        OutputRoute.WIRED_AUX -> RouteProfile(bassBoostDb = 2.0f, dialogBoostDb = 1.0f, widenerMult = 1.0f)
        // USB-C: DAC dedicado, sin compensación necesaria.
        OutputRoute.USB -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 0.5f, widenerMult = 1.0f)
        OutputRoute.SPEAKER, OutputRoute.UNKNOWN -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 0f, widenerMult = 1.0f)
    }

    // Umbral de bitrate bajo (kbps) por debajo del cual SBC degrada
    // audiblemente banda 2-4kHz y separación estéreo.
    private const val BT_LOW_BITRATE_KBPS = 200

    private fun currentBtBitrateKbps(): Int? {
        val am = audioManager ?: return null
        return try {
            // API no pública/vendor-specific (sin garantía en todos los OEM);
            // se envuelve en try/catch y se degrada a null sin romper nada.
            val raw = am.getParameters("bt_codec_bitrate")
            raw.substringAfter("=", "").trim().toIntOrNull()
        } catch (e: Exception) {
            Log.w(TAG, "No se pudo leer bt_codec_bitrate: ${e.message}")
            null
        }
    }

    private fun btProfile(): RouteProfile {
        val bitrate = currentBtBitrateKbps()
        return if (bitrate != null && bitrate < BT_LOW_BITRATE_KBPS) {
            // SBC en bitrate bajo: ensanchado casi anulado, diálogo elevado
            // más agresivo que el perfil BT estándar.
            Log.i(TAG, "BT bitrate bajo detectado: ${bitrate}kbps -> perfil low-bitrate")
            RouteProfile(bassBoostDb = 0f, dialogBoostDb = 4.5f, widenerMult = 0.5f)
        } else {
            RouteProfile(bassBoostDb = 0f, dialogBoostDb = 3.5f, widenerMult = 0.65f)
        }
    }

    private fun applyRoute(route: OutputRoute) {
        if (route == currentRoute) return
        currentRoute = route
        val p = profileFor(route)
        Log.i(TAG, "Ruta de salida: $route -> bassBoost=${p.bassBoostDb}dB dialogBoost=${p.dialogBoostDb}dB widenerMult=${p.widenerMult}")
        AudioEngine.nativeSetRouteProfileStatic(p.bassBoostDb, p.dialogBoostDb, p.widenerMult)
        // (implementado vía AudioEngine.nativeSetRouteProfileJni -> control_set_route_profile)
    }
}
