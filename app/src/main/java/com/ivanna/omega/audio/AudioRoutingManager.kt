package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build
import android.util.Log

/**
 * AudioRoutingManager — deteccion y forzado de ruta de salida.
 *
 * ANTES: `restoreDefaultRouting()` volvia a poner `isSpeakerphoneOn=true` y
 * `isBluetoothA2dpOn=true` de golpe — dos APIs deprecadas desde API 26 y 34
 * respectivamente, y ambas cambian la ruta ACTIVA del usuario. Si el usuario
 * estaba oyendo por auriculares y la app llamaba a restoreDefault, la salida
 * saltaba al altavoz de golpe (isSpeakerphoneOn afecta a la sesion de
 * voz, no solo al modo speaker). Ademas se usaba una variable local
 * `audioManager` en cada metodo, sin obtener el AudioManager una sola vez.
 *
 * AHORA:
 *  - `detectOutputRoute()` sin cambios de comportamiento (mismo orden de
 *    prioridad, mismos tipos) pero envolviendo el `getDevices()` en
 *    try/catch: algunos OEM (Huawei EMUI) lanzan SecurityException si el
 *    permiso RECORD_AUDIO todavia no ha sido otorgado y la app se ejecuta
 *    antes del prompt de runtime.
 *  - `forceUsbDacRouting()` ahora NO pisa isSpeakerphoneOn / isBluetoothA2dpOn
 *    globales — solo llama a AudioTrack.setPreferredDevice() (API 24+) sobre
 *    el track del propio pipeline, que es lo unico que un usuario espera
 *    cambiar. Pisar los toggles globales afectaba a otras apps (Spotify,
 *    llamadas VoIP) y en API 30+ requiere MODIFY_PHONE_STATE.
 *  - `restoreDefaultRouting()` deja de tocar isSpeakerphoneOn/A2dp: solo
 *    limpia el preferredDevice del AudioTrack propio, devolviendo el
 *    routing al que Android eligio por politica del sistema — que es lo
 *    correcto cuando el usuario desconecta el DAC.
 *  - Un solo helper `audioManager()` con log-en-fallo (si el servicio no
 *    esta disponible en el proceso, se registra y se devuelve null en vez
 *    de propagar un NPE hasta el UI thread).
 */
object AudioRoutingManager {

    private const val TAG = "AudioRoutingManager"

    private fun audioManager(context: Context): AudioManager? =
        try {
            context.applicationContext.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        } catch (t: Throwable) {
            Log.w(TAG, "getSystemService(AUDIO_SERVICE): ${t.message}")
            null
        }

    /** 4D: detecta la salida de audio activa de mayor prioridad.
     *  Orden: USB-DAC > Bluetooth A2DP > Auriculares con cable > Altavoz. */
    fun detectOutputRoute(context: Context): String {
        val am = audioManager(context) ?: return "Speaker"
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return "Speaker"

        // Huawei EMUI y algunos ROMs derivados lanzan SecurityException aqui
        // si RECORD_AUDIO no esta concedido todavia — la app puede consultar
        // la ruta antes del prompt de runtime al arrancar por primera vez.
        val devices = try {
            am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        } catch (t: Throwable) {
            Log.w(TAG, "getDevices(OUTPUTS): ${t.message}")
            return "Speaker"
        }

        // Prioridad estable: recorrido unico, se elige el de mayor rango.
        var best = "Speaker"
        var bestRank = 0
        for (d in devices) {
            val rank = when (d.type) {
                AudioDeviceInfo.TYPE_USB_HEADSET,
                AudioDeviceInfo.TYPE_USB_DEVICE -> 4
                AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> 3
                AudioDeviceInfo.TYPE_WIRED_HEADSET,
                AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> 2
                AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> 1
                else -> 0
            }
            val label = when (d.type) {
                AudioDeviceInfo.TYPE_USB_HEADSET,
                AudioDeviceInfo.TYPE_USB_DEVICE -> "USB-DAC"
                AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "Bluetooth"
                AudioDeviceInfo.TYPE_WIRED_HEADSET,
                AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> "Headphone"
                AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "Speaker"
                else -> null
            } ?: continue
            if (rank > bestRank) { bestRank = rank; best = label }
        }
        return best
    }

    /**
     * Prefiere el DAC USB como salida del AudioTrack dado (API 24+). Solo
     * actua sobre el track del propio pipeline — no toca isSpeakerphoneOn
     * ni isBluetoothA2dpOn globales (esos afectan a otras apps y en API 30+
     * requieren MODIFY_PHONE_STATE). Devuelve false si no hay USB colgado o
     * si el AudioTrack no es re-enrutable.
     */
    fun forceUsbDacRouting(context: Context, audioTrack: AudioTrack? = null): Boolean {
        val am = audioManager(context) ?: return false
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return false
        if (audioTrack == null) return false

        val devices = try {
            am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        } catch (t: Throwable) {
            Log.w(TAG, "forceUsbDacRouting getDevices: ${t.message}"); return false
        }
        val usb = devices.firstOrNull {
            it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
            it.type == AudioDeviceInfo.TYPE_USB_HEADSET
        } ?: run {
            Log.i(TAG, "forceUsbDacRouting: no hay USB colgado — no-op")
            return false
        }

        return runCatching {
            val ok = audioTrack.setPreferredDevice(usb)
            if (!ok) Log.w(TAG, "AudioTrack.setPreferredDevice(USB) devolvio false — el track puede no ser re-enrutable")
            else Log.i(TAG, "AudioTrack anclado a USB-DAC: ${usb.productName}")
            ok
        }.getOrElse {
            Log.w(TAG, "forceUsbDacRouting: ${it.message}"); false
        }
    }

    /**
     * Devuelve el routing al que Android elija por politica del sistema:
     * solo limpia el preferredDevice del AudioTrack dado (setPreferredDevice(null)
     * = "sin anclaje", el sistema resuelve como si nunca se hubiera fijado). NO
     * toca los toggles globales isSpeakerphoneOn / isBluetoothA2dpOn —
     * pisarlos rompia la ruta activa del usuario en otras apps.
     */
    fun restoreDefaultRouting(context: Context, audioTrack: AudioTrack? = null): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return true
        // Solo por efecto de asegurarse de que hay servicio antes de tocar el track.
        audioManager(context) ?: return false
        return runCatching {
            audioTrack?.setPreferredDevice(null)
            Log.i(TAG, "AudioTrack liberado — routing devuelto a politica del sistema")
            true
        }.getOrElse {
            Log.w(TAG, "restoreDefaultRouting: ${it.message}"); false
        }
    }
}
