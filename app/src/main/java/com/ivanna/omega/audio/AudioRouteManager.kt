package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.concurrent.atomic.AtomicLong

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

    // Handler unico del hilo main + generacion monotona para invalidar el
    // callback de "restaurar wet=1" si la ruta cambia antes de que expire la
    // ventana de 150ms. Sin esto: si el usuario desconecta el DAC dentro de
    // esos 150ms, el postDelayed anterior seguia vivo y pisaba wet=1 sobre un
    // history recien flusheado de una ruta que ya no era USB — tronido audible.
    private val mainHandler = Handler(Looper.getMainLooper())
    private val hrtfRestoreToken = AtomicLong(0L)
    private var pendingHrtfRestore: Runnable? = null

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
        val previousRoute = currentRoute
        currentRoute = route
        val p = profileFor(route)
        Log.i(TAG, "Ruta de salida: $route -> bassBoost=${p.bassBoostDb}dB dialogBoost=${p.dialogBoostDb}dB widenerMult=${p.widenerMult}")
        AudioEngine.nativeSetRouteProfileStatic(p.bassBoostDb, p.dialogBoostDb, p.widenerMult)
        // (implementado vía AudioEngine.nativeSetRouteProfileJni -> control_set_route_profile)

        // FIX (unificación de rutas): el mismo perfil también se manda a
        // Ruta B (módulo Magisk / omega_daemon), que antes nunca se
        // enteraba de la ruta de salida activa — Spotify/YouTube sonaban
        // sin compensación de BT SBC/AAC ni de rolloff de graves en AUX.
        com.ivanna.omega.magisk.OmegaEngineBridge.setRouteProfile(
            p.bassBoostDb, p.dialogBoostDb, p.widenerMult
        )

        // FIX DAC USB-C: al conectar un DAC USB-C, el historial de convolución
        // HRTF (m_histL/m_histR) contiene muestras de la ruta anterior. La
        // siguiente llamada a processBinauralScene() las convoluciona y produce
        // el ruido "tssss" (canal de TV sin señal). Secuencia de fix:
        //   1. setWetDry(0) → bypass inmediato, sin convolución esta iteración
        //   2. flushHistory() → memset(0) del historial: corta la fuente de ruido
        //   3. postDelayed(150ms) → restaurar wet=1.0 cuando el history esté limpio
        //      (2–3 frames a 48kHz son ~16ms; 150ms es margen amplio para cualquier SR)
        // Al salir del USB: restaurar wet=1.0 inmediatamente (history ya tiene
        // muestras limpias del nuevo dispositivo de salida).
        if (route == OutputRoute.USB) {
            // Cancela cualquier restore pendiente de una rotacion previa (p.ej.
            // usuario que hace USB->AUX->USB rapido) y adquiere token nuevo:
            // solo el runnable que porte este token podra restaurar wet=1.
            cancelPendingHrtfRestore()
            val myToken = hrtfRestoreToken.incrementAndGet()
            AudioEngine.nativeSetHrtfWetDryStatic(0f)
            AudioEngine.nativeFlushHrtfHistoryStatic()
            val r = Runnable {
                // Doble guard: seguimos en USB Y el token no fue invalidado por
                // una rotacion posterior (dentro de la ventana de 150ms).
                if (currentRoute == OutputRoute.USB &&
                    hrtfRestoreToken.get() == myToken) {
                    AudioEngine.nativeSetHrtfWetDryStatic(1f)
                    Log.i(TAG, "HRTF restaurado a wet=1.0 tras flush DAC USB-C (token=$myToken)")
                }
                pendingHrtfRestore = null
            }
            pendingHrtfRestore = r
            mainHandler.postDelayed(r, 150L)
        } else if (previousRoute == OutputRoute.USB) {
            // Saliendo de USB: cancelar el restore pendiente antes de tocar
            // wet=1 en la nueva ruta — evita doble escritura y elimina la
            // ventana en la que el runnable viejo podria haberse ejecutado
            // *entre* este set y una siguiente rotacion.
            cancelPendingHrtfRestore()
            AudioEngine.nativeSetHrtfWetDryStatic(1f)
            Log.i(TAG, "HRTF restaurado a wet=1.0 al salir de ruta USB")
        }
    }

    /**
     * Cancela el restore pendiente de wet=1 tras un flush por rotacion a USB.
     * Se llama tanto al entrar de nuevo a USB (nueva secuencia empieza limpia)
     * como al salir de USB (evita que un runnable atrasado pise el wet=1 que
     * ya escribio la rama de salida). Ademas invalida el token monotono para
     * que un runnable ya encolado que consiga entrar antes del removeCallbacks
     * detecte la invalidacion y no toque el motor.
     */
    private fun cancelPendingHrtfRestore() {
        pendingHrtfRestore?.let { mainHandler.removeCallbacks(it) }
        pendingHrtfRestore = null
        hrtfRestoreToken.incrementAndGet()
    }
}
