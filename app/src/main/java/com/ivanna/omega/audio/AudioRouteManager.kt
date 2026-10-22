package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ivanna.omega.safmus.SafmusAdapterPolicy
import com.ivanna.omega.safmus.SafmusHardwareCoordinator
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
    @Volatile private var currentRoute: OutputRoute = OutputRoute.UNKNOWN
    // Invalidates asynchronous codec lookups when the output route changes.
    private val routeGeneration = AtomicLong(0L)
    // Application context (no la Activity) para BluetoothCodecDetector —
    // se guarda una sola vez en start(), nunca una referencia de Activity,
    // para no filtrar memoria en este singleton de vida larga.
    private var appContextRef: Context? = null

    // Handler unico del hilo main + generacion monotona para invalidar el
    // callback de "restaurar wet=1" si la ruta cambia antes de que expire la
    // ventana de 150ms. Sin esto: si el usuario desconecta el DAC dentro de
    // esos 150ms, el postDelayed anterior seguia vivo y pisaba wet=1 sobre un
    // history recien flusheado de una ruta que ya no era USB — tronido audible.
    private val mainHandler = Handler(Looper.getMainLooper())
    private val hrtfRestoreToken = AtomicLong(0L)
    private var pendingHrtfRestore: Runnable? = null

    @Synchronized
    fun start(context: Context) {
        appContextRef = context.applicationContext
        val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return
        // Idempotente: un segundo start() no debe dejar el callback anterior vivo
        // (duplicaría applyRoute en cada hotplug y filtraría la instancia).
        deviceCallback?.let { runCatching { am.unregisterAudioDeviceCallback(it) } }
        deviceCallback = null
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

    @Synchronized
    fun stop() {
        // Invalidate any outstanding codec lookup before releasing route ownership.
        routeGeneration.incrementAndGet()
        currentRoute = OutputRoute.UNKNOWN
        BluetoothAudioProfiler.invalidate()
        // Un restore HRTF diferido no debe dispararse tras detener el manager.
        pendingHrtfRestore?.let { mainHandler.removeCallbacks(it) }
        pendingHrtfRestore = null
        val am = audioManager ?: return
        deviceCallback?.let { am.unregisterAudioDeviceCallback(it) }
        deviceCallback = null
        audioManager = null
        appContextRef = null
    }

    fun detectOutputRoute(): OutputRoute {
        val am = audioManager ?: return OutputRoute.UNKNOWN
        return detectOutputRoute(am)
    }

    /**
     * Re-evalúa inmediatamente la ruta de salida activa y aplica su perfil
     * en el pipeline unificado (AudioEngine + OmegaEngineBridge + HRTF).
     * Llamado por UsbAudioProManager / UsbDacAttachReceiver al conectar o
     * desconectar un DAC Tipo-C para que use exactamente el mismo path.
     */
    fun refreshRoute() {
        val am = audioManager ?: return
        applyRoute(detectOutputRoute(am))
    }

    /** Re-apply only the SAFMUS-adjusted DSP profile; do not reset route histories. */
    fun refreshResourceBudgetProfile() {
        val route = currentRoute
        if (route == OutputRoute.UNKNOWN) return
        if (route == OutputRoute.BLUETOOTH) {
            // Never block the caller on A2DP proxy acquisition or vendor parameters.
            applyProfileFor(route, BluetoothRouteProfilePolicy.fallback())
            val generation = routeGeneration.incrementAndGet()
            appContextRef?.let { refreshBluetoothCodecProfileAsync(it, generation) }
        } else {
            applyProfileFor(route)
        }
    }

    fun detectOutputRoute(am: AudioManager): OutputRoute {
        val outputs = runCatching { am.getDevices(AudioManager.GET_DEVICES_OUTPUTS) }
            .getOrNull().orEmpty()
        return OutputRoutePolicy.select(
            usbConnected = outputs.any {
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET ||
                    it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                    it.type == AudioDeviceInfo.TYPE_USB_ACCESSORY
            },
            bluetoothConnected = outputs.any {
                it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                    it.type == AudioDeviceInfo.TYPE_BLE_HEADSET ||
                    it.type == AudioDeviceInfo.TYPE_BLE_SPEAKER
            },
            wiredConnected = outputs.any {
                it.type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
                    it.type == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
                    it.type == AudioDeviceInfo.TYPE_AUX_LINE
            },
            speakerConnected = outputs.any { it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER }
        )
    }

    fun profileFor(route: OutputRoute): RouteProfile = when (route) {
        // SBC/AAC pierden presencia 2-4kHz y el estéreo se degrada al
        // recodificar; se compensa diálogo y se reduce ancho.
        // Public profile query must stay non-blocking; async lookup refines this fallback.
        OutputRoute.BLUETOOTH -> BluetoothRouteProfilePolicy.fallback()
        // AUX cableado y USB-C DAC: mismo path unificado de línea/audífonos
        // con respuesta limpia y compensación coherente.
        OutputRoute.WIRED_AUX,
        OutputRoute.USB -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 0.5f, widenerMult = 1.0f)
        // Altavoz integrado: no forzar graves (evita excursion/limitacion en
        // transductores pequeños); realce moderado de presencia y anchura
        // conservadora para conservar inteligibilidad sin empujar el clipping.
        OutputRoute.SPEAKER -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 1.5f, widenerMult = 0.85f)
        OutputRoute.UNKNOWN -> RouteProfile(bassBoostDb = 0f, dialogBoostDb = 0f, widenerMult = 1.0f)
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

    /** Conservative profile used until the real A2DP codec is resolved off-thread. */
    private fun fallbackBluetoothProfile(): RouteProfile =
        BluetoothRouteProfilePolicy.fallback(currentBtBitrateKbps())

    /**
     * A2DP proxy acquisition is asynchronous and may wait up to the detector timeout.
     * Resolve it on a worker, then publish only if this is still the active route generation.
     */
    private fun refreshBluetoothCodecProfileAsync(context: Context, generation: Long) {
        Thread({
            // Match the codec query to the A2DP output selected from Android's device list.
            // If Android hides the address, the detector safely falls back to its default lookup.
            val selectedA2dp = runCatching {
                val manager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
                manager?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
                    ?.firstOrNull { it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP }
            }.getOrNull()
            val selectedAddress = selectedA2dp?.address?.takeIf { it.isNotBlank() }
            val info = if (selectedAddress == null) {
                // Without a reliable address, do not borrow another connected headset's codec.
                BluetoothCodecDetector.CodecInfo(BluetoothCodecDetector.RealCodec.UNAVAILABLE, 0, 0)
            } else runCatching {
                BluetoothCodecDetector.detectActiveCodec(context, selectedAddress)
            }.getOrElse {
                    Log.w(TAG, "BT codec lookup failed; keeping conservative route profile", it)
                    BluetoothCodecDetector.CodecInfo(
                        BluetoothCodecDetector.RealCodec.UNAVAILABLE, 0, 0
                    )
                }
            val profile = if (
                info.codec != BluetoothCodecDetector.RealCodec.UNAVAILABLE &&
                info.codec != BluetoothCodecDetector.RealCodec.UNKNOWN
            ) {
                Log.i(TAG, "BT codec resolved asynchronously: ${info.codec} @ ${info.sampleRateHz}Hz/${info.bitsPerSample}bit")
                BluetoothRouteProfilePolicy.forCodec(info.codec)
            } else {
                fallbackBluetoothProfile()
            }
            if (routeGeneration.get() == generation && currentRoute == OutputRoute.BLUETOOTH) {
                applyProfileFor(OutputRoute.BLUETOOTH, profile)
            } else {
                Log.d(TAG, "Discarding stale BT codec result for route generation $generation")
            }
        }, "BtCodecProfileRefresh").start()
    }

    private fun applyProfileFor(route: OutputRoute, profileOverride: RouteProfile? = null) {
        val baseProfile = profileOverride ?: profileFor(route)
        val load = when (SafmusHardwareCoordinator.lastSnapshot?.budget) {
            SafmusHardwareCoordinator.Budget.CONSERVATIVE -> SafmusAdapterPolicy.Load.CONSERVATIVE
            SafmusHardwareCoordinator.Budget.SAFE -> SafmusAdapterPolicy.Load.SAFE
            else -> SafmusAdapterPolicy.Load.NORMAL
        }
        val p = SafmusAdapterPolicy.optimizeRouteProfile(baseProfile, load)
        Log.i(TAG, "Ruta de salida: $route SAFMUS=$load -> perfil DSP aplicado")
        AudioEngine.nativeSetRouteProfileStatic(p.bassBoostDb, p.dialogBoostDb, p.widenerMult)
        com.ivanna.omega.magisk.OmegaEngineBridge.setRouteProfile(
            p.bassBoostDb, p.dialogBoostDb, p.widenerMult
        )
    }

    private fun applyRoute(route: OutputRoute) {
        if (route == currentRoute) return
        val previousRoute = currentRoute
        val generation = routeGeneration.incrementAndGet()
        currentRoute = route
        appContextRef?.let { ctx ->
            runCatching { AudioPipeline.syncHardwareSampleRate(ctx) }
            if (route == OutputRoute.BLUETOOTH) BluetoothAudioProfiler.invalidate()
            Thread({
                runCatching { BluetoothAudioProfiler.refresh(ctx) }
            }, "BtAudioProfilerRefresh").start()
        }
        if (route == OutputRoute.BLUETOOTH) {
            // Apply immediately with a bounded fallback; do not block the device callback.
            applyProfileFor(route, BluetoothRouteProfilePolicy.fallback())
            appContextRef?.let { refreshBluetoothCodecProfileAsync(it, generation) }
        } else {
            applyProfileFor(route)
        }
        // §0.1: Activar inmediatamente las etapas exigidas por el perfil de ruta
        // ANTES del primer bloque de audio (y en cada cambio de salida física).
        appContextRef?.let { ctx ->
            runCatching { RouteDspCalibrator.onRouteChanged(ctx, route) }
        }

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
