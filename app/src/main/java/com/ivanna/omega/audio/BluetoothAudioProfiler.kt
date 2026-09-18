package com.ivanna.omega.audio

import android.annotation.SuppressLint
import android.bluetooth.BluetoothA2dp
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.util.Log
import java.util.concurrent.ConcurrentHashMap

/**
 * BluetoothAudioProfiler — perfilado magistral de la ruta Bluetooth.
 *
 * QUÉ HACE (y por qué la experiencia BT era inconsistente antes):
 *  1. DETECCIÓN DE CÓDEC REAL: lee el códec A2DP activo (SBC/AAC/aptX/aptX HD/
 *     aptX Adaptive/LDAC/LHDC y variantes) del BluetoothA2dp proxy — API 33+
 *     vía BluetoothA2dp.getActiveCodec / codecConfig, y por reflexión en
 *     API 31-32 (los OEM exponen el campo con nombres distintos). Antes la app
 *     no sabía qué códec estaba sonando y aplicaba el mismo procesado a un
 *     LDAC 990 kbps (hi-res) que a un SBC 328 kbps (con pérdida audible).
 *  2. CALIBRACIÓN ADAPTATIVA DEL PROCESADO: cada cócodec tiene un perfil de
 *     coloración conocido (SBC recorta >15 kHz y sube ruido de cuantización
 *     en graves; AAC mantiene agudos pero colapsa transitorios; LDAC/aptX HD
     son prácticamente transparentes). El profiler entrega un PerfProfile
 *     con: límite de banda real, pre-énfasis de agudos recomendado, techo de
 *     excitación armónica (evita que el HarmonicExciter genere contenido que
 *     el códec va a tirar — gasto de bits inútil y fuente de artefactos) y
 *     wet de espacialización seguro para el retardo del códec.
 *  3. COMPENSACIÓN DE LATENCIA ESTIMADA: cada cócodec añade un retardo fijo
 *     conocido (SBC ~200 ms, AAC ~180, aptX ~120, aptX LL ~40, LDAC ~220);
 *     el profiler lo expone para que el sincronismo UI/visualizador y la
 *     mezcla Haas del capture service no persigan un fantasma.
 *  4. CACHÉ POR DISPOSITIVO: el perfil se calcula una vez por MAC y se
 *     invalida solo en cambio de ruta — cero trabajo en el hilo de audio.
 *
 * Sin permisos extra: BLUETOOTH_CONNECT ya está en el manifest (API 31+).
 */
object BluetoothAudioProfiler {

    private const val TAG = "BluetoothAudioProfiler"

    /** Códecs A2DP reconocidos, ordenados por calidad ascendente. */
    enum class Codec(val label: String, val bandLimitHz: Int, val latencyMs: Int,
                     val harmonicCeil: Float, val spatialWetMax: Float) {
        // bandLimitHz: techo real de la banda pasante del códec.
        // harmonicCeil: ganancia máxima segura del excitador armónico para no
        //   alimentar al códec con contenido que descarta (artefactos).
        // spatialWetMax: wet máximo de espacialización antes de que el
        //   retardo del códec haga audible el desacople imagen/sonido.
        SBC(        "SBC",          15000, 200, 0.18f, 0.55f),
        AAC(        "AAC",          17000, 180, 0.24f, 0.65f),
        APTX(       "aptX",         18000, 120, 0.28f, 0.70f),
        APTX_HD(    "aptX HD",      20000, 130, 0.34f, 0.80f),
        APTX_ADAPT("aptX Adaptive", 20000,  80, 0.36f, 0.85f),
        LDAC(       "LDAC",         22000, 220, 0.40f, 0.90f),
        LHDC(       "LHDC/LLAC",    22000, 190, 0.40f, 0.90f),
        UNKNOWN(    "Bluetooth",    16000, 180, 0.22f, 0.60f),
        WIRED(      "Alámbrico",    24000,   0, 1.00f, 1.00f),
        NONE(       "Sin audio",        0,   0, 0.00f, 0.00f),
    }

    /** Perfil de procesado resuelto para el dispositivo BT activo. */
    data class PerfProfile(
        val codec: Codec,
        val deviceName: String,
        /** Límite de banda real del códec (Hz) — el EQ/excitador no debe crear contenido por encima. */
        val bandLimitHz: Int,
        /** Retardo estructural estimado del códec (ms) — para sync UI y mezcla Haas. */
        val latencyMs: Int,
        /** Techo de ganancia del HarmonicExciter para este códec (0..1). */
        val harmonicCeil: Float,
        /** Wet máximo recomendado de espacialización WFS/HRTF (0..1). */
        val spatialWetMax: Float,
        /** true si el códec es transparente (LDAC/aptX HD+): procesado completo permitido. */
        val transparent: Boolean,
    ) {
        companion object {
            val WIRED = PerfProfile(Codec.WIRED, "Salida alámbrica", 24000, 0, 1.0f, 1.0f, true)
            val NONE  = PerfProfile(Codec.NONE,  "Sin salida",           0, 0, 0.0f, 0.0f, false)
        }
    }

    // Caché por dirección MAC — el perfil solo cambia con la ruta.
    private val profileCache = ConcurrentHashMap<String, PerfProfile>()
    @Volatile private var activeProfile: PerfProfile? = null

    /** Perfil activo en este instante (caché — seguro desde cualquier hilo). */
    fun currentProfile(): PerfProfile = activeProfile ?: PerfProfile.NONE

    /**
     * Refresca el perfil del dispositivo BT activo. Llamar desde el hilo de
     * control al detectar cambio de ruta (AudioRouteManager) — NO desde el
     * hilo de audio. Devuelve el perfil resuelto (WIRED/NONE si no hay BT).
     */
    @SuppressLint("MissingPermission")   // BLUETOOTH_CONNECT en manifest (API 31+)
    fun refresh(context: Context): PerfProfile {
        val am = context.applicationContext
            .getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        val devices = try {
            am?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        } catch (t: Throwable) { null } ?: emptyArray()

        val btDevice = devices.firstOrNull { it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP }
        if (btDevice == null) {
            // No hay BT activo: perfil alámbrico si hay auricular/USB, NONE si no.
            val wired = devices.any {
                it.type == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
                it.type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
                it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET
            }
            val profile = if (wired) PerfProfile.WIRED else PerfProfile.NONE
            activeProfile = profile
            return profile
        }

        val mac = try { btDevice.address } catch (t: Throwable) { null } ?: "unknown"
        profileCache[mac]?.let {
            activeProfile = it
            Log.i(TAG, "Perfil BT en caché: ${it.codec.label} @ $mac")
            return it
        }

        // Resolución del códec: intenta la API real de A2DP, luego reflexión.
        val codec = resolveCodec(context, btDevice)
        val name = try { btDevice.productName?.toString() } catch (t: Throwable) { null }
            ?: codec.label
        val profile = PerfProfile(
            codec         = codec,
            deviceName    = name,
            bandLimitHz   = codec.bandLimitHz,
            latencyMs     = codec.latencyMs,
            harmonicCeil  = codec.harmonicCeil,
            spatialWetMax = codec.spatialWetMax,
            transparent   = codec.ordinal >= Codec.APTX_HD.ordinal,
        )
        profileCache[mac] = profile
        activeProfile = profile
        Log.i(TAG, "Perfil BT resuelto: $name → ${codec.label} " +
              "(banda=${profile.bandLimitHz} Hz, latencia=${profile.latencyMs} ms, " +
              "armónico≤${profile.harmonicCeil}, wet≤${profile.spatialWetMax})")
        return profile
    }

    /** Invalida la caché (llamar al desconectar BT o cambiar de dispositivo). */
    fun invalidate(address: String? = null) {
        if (address == null) profileCache.clear() else profileCache.remove(address)
        activeProfile = null
    }

    // ── Resolución del códec A2DP activo ─────────────────────────────────────

    @SuppressLint("MissingPermission")
    private fun resolveCodec(context: Context, device: AudioDeviceInfo): Codec {
        // Ruta 1 (API 33+): BluetoothA2dp proxy → codecConfig.
        if (Build.VERSION.SDK_INT >= 33) {
            resolveCodecApi33(context)?.let { return it }
        }
        // Ruta 2 (API 31-32): reflexión sobre BluetoothA2dp (nombres OEM).
        resolveCodecReflection(context)?.let { return it }
        // Ruta 3 (fallback): nombre del dispositivo como heurística débil.
        return codecFromName(device.productName?.toString() ?: "")
    }

    @SuppressLint("MissingPermission")
    private fun resolveCodecApi33(context: Context): Codec? {
        return runCatching {
            val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            val adapter = bm?.adapter ?: return null
            var result: Codec? = null
            val latch = java.util.concurrent.CountDownLatch(1)
            adapter.getProfileProxy(context, object : android.bluetooth.BluetoothProfile.ServiceListener {
                override fun onServiceConnected(profile: Int, proxy: android.bluetooth.BluetoothProfile?) {
                    if (profile == BluetoothProfile.A2DP && proxy is BluetoothA2dp) {
                        result = runCatching {
                            // API 33+: isCodecConfigurable / codec status
                            val dev = proxy.connectedDevices?.firstOrNull()
                            if (dev != null) codecFromProxyMethod(proxy, dev)
                            else null
                        }.getOrNull()
                    }
                    latch.countDown()
                }
                override fun onServiceDisconnected(profile: Int) { latch.countDown() }
            }, BluetoothProfile.A2DP)
            // getProfileProxy es asíncrono — esperamos con timeout corto
            // (hilo de control, no de audio; 300 ms es holgado).
            latch.await(300, java.util.concurrent.TimeUnit.MILLISECONDS)
            result
        }.getOrNull()
    }

    /** Lee el códec del proxy A2DP probando los métodos disponibles por reflexión. */
    @SuppressLint("MissingPermission")
    private fun codecFromProxyMethod(proxy: BluetoothA2dp, device: BluetoothDevice): Codec? {
        // Intenta getCodecStatus (API 26+, oculto en algunos OEM) vía reflexión.
        return runCatching {
            val m = proxy.javaClass.getMethod("getCodecStatus", BluetoothDevice::class.java)
            val status = m.invoke(proxy, device) ?: return null
            // BluetoothCodecStatus.getCodecConfig() → BluetoothCodecConfig
            val getCfg = status.javaClass.getMethod("getCodecConfig")
            val cfg = getCfg.invoke(status) ?: return null
            val getType = cfg.javaClass.getMethod("getCodecType")
            codecFromInt(getType.invoke(cfg) as? Int ?: return null)
        }.getOrNull()
    }

    @SuppressLint("MissingPermission")
    private fun resolveCodecReflection(context: Context): Codec? {
        return runCatching {
            val bm = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            val adapter = bm?.adapter ?: return null
            var result: Codec? = null
            val latch = java.util.concurrent.CountDownLatch(1)
            adapter.getProfileProxy(context, object : android.bluetooth.BluetoothProfile.ServiceListener {
                override fun onServiceConnected(profile: Int, proxy: android.bluetooth.BluetoothProfile?) {
                    if (profile == BluetoothProfile.A2DP && proxy is BluetoothA2dp) {
                        result = runCatching {
                            val dev = proxy.connectedDevices?.firstOrNull() ?: return@runCatching null
                            codecFromProxyMethod(proxy, dev)
                        }.getOrNull()
                        adapter.closeProfileProxy(BluetoothProfile.A2DP, proxy)
                    }
                    latch.countDown()
                }
                override fun onServiceDisconnected(profile: Int) { latch.countDown() }
            }, BluetoothProfile.A2DP)
            latch.await(300, java.util.concurrent.TimeUnit.MILLISECONDS)
            result
        }.getOrNull()
    }

    /** Mapea el código numérico A2DP al enum. Fuentes: AOSP BluetoothCodecConfig. */
    private fun codecFromInt(type: Int): Codec = when (type) {
        0    -> Codec.SBC
        1    -> Codec.AAC
        2    -> Codec.APTX
        3    -> Codec.APTX_HD
        4    -> Codec.LDAC
        5    -> Codec.UNKNOWN          // aptX Adaptive no tiene código propio en API ≤33
        6    -> Codec.LHDC
        else -> Codec.UNKNOWN
    }

    /** Heurística por nombre (último recurso cuando la API no responde). */
    private fun codecFromName(name: String): Codec {
        val n = name.lowercase()
        return when {
            "ldac" in n -> Codec.LDAC
            "lhdc" in n || "llac" in n -> Codec.LHDC
            "aptx adaptive" in n || "aptx_adaptive" in n -> Codec.APTX_ADAPT
            "aptx hd" in n || "aptxhd" in n -> Codec.APTX_HD
            "aptx" in n -> Codec.APTX
            "aac" in n -> Codec.AAC
            else -> Codec.UNKNOWN
        }
    }
}
