package com.ivanna.omega.audio

import android.bluetooth.BluetoothA2dp
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothCodecConfig
import android.bluetooth.BluetoothCodecStatus
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import android.Manifest
import android.content.pm.PackageManager
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * BluetoothCodecDetector — identifica el códec A2DP REAL en uso mediante
 * la API pública `BluetoothA2dp.getCodecStatus()` (disponible desde API 26,
 * este proyecto tiene minSdk=28), en vez del parámetro no público y
 * específico de fabricante (`bt_codec_bitrate`) que AudioRouteManager usaba
 * como única señal — ese parámetro está documentado ahí mismo como "sin
 * garantía en todos los OEM" porque literalmente no es API pública, es un
 * vendor string que cada fabricante puede o no implementar/nombrar igual.
 *
 * Códecs A2DP reales tienen calidad MUY distinta entre sí — no es un
 * espectro continuo de "bitrate alto/bajo": SBC (baseline obligatorio,
 * ~192-345kbps con artefactos de banda conocidos), AAC (mejor pero con
 * calidad de encoder que varía fuerte por fabricante Android — problema
 * documentado de la plataforma, no del códec en sí), aptX/aptX HD/aptX
 * Adaptive (352-660kbps, sin artefactos de banda de SBC), LDAC (hasta
 * 990kbps, el más transparente disponible en Android), Opus (API 33+,
 * eficiente y limpio). Cada uno necesita su propio perfil de compensación,
 * no un umbral binario de bitrate.
 */
object BluetoothCodecDetector {
    private const val TAG = "BtCodecDetector"
    private const val PROXY_TIMEOUT_MS = 300L

    enum class RealCodec { SBC, AAC, APTX, APTX_HD, APTX_ADAPTIVE, LDAC, OPUS, UNKNOWN, UNAVAILABLE }

    data class CodecInfo(
        val codec: RealCodec,
        val sampleRateHz: Int,
        val bitsPerSample: Int,
    )

    /**
     * Consulta el códec A2DP activo AHORA MISMO. Bloqueante hasta
     * [PROXY_TIMEOUT_MS] porque `getProfileProxy` es asíncrono por diseño
     * de la API de Android — se usa un CountDownLatch acotado en vez de
     * dejarlo indefinido, para que un fallo de conexión al proxy nunca
     * cuelgue el hilo de detección de ruta (que corre en el callback de
     * AudioDeviceCallback, no en el hilo de audio — bloqueo corto aquí es
     * aceptable, igual que ya hace AudioRouteManager con la llamada a
     * getParameters() que reemplaza).
     *
     * Devuelve CodecInfo(UNAVAILABLE, ...) si el permiso no está concedido,
     * si no hay proxy A2DP disponible, o si el timeout expira — nunca
     * lanza, nunca bloquea más de [PROXY_TIMEOUT_MS].
     */
    fun detectActiveCodec(context: Context): CodecInfo {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT)
            != PackageManager.PERMISSION_GRANTED
        ) {
            Log.w(TAG, "BLUETOOTH_CONNECT no concedido — sin detección real de códec")
            return CodecInfo(RealCodec.UNAVAILABLE, 0, 0)
        }

        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null || !adapter.isEnabled) {
            return CodecInfo(RealCodec.UNAVAILABLE, 0, 0)
        }

        val result = AtomicReference<CodecInfo>(CodecInfo(RealCodec.UNAVAILABLE, 0, 0))
        val latch = CountDownLatch(1)
        var proxyRef: BluetoothA2dp? = null

        val listener = object : BluetoothProfile.ServiceListener {
            override fun onServiceConnected(profile: Int, proxy: BluetoothProfile) {
                try {
                    val a2dp = proxy as? BluetoothA2dp
                    proxyRef = a2dp
                    val device = a2dp?.connectedDevices?.firstOrNull()
                    if (a2dp != null && device != null) {
                        val status: BluetoothCodecStatus? = a2dp.getCodecStatus(device)
                        val cfg: BluetoothCodecConfig? = status?.codecConfig
                        result.set(mapCodecConfig(cfg))
                    }
                } catch (e: SecurityException) {
                    Log.w(TAG, "Sin permiso runtime real pese al check estático: ${e.message}")
                } catch (e: Exception) {
                    Log.w(TAG, "detectActiveCodec: ${e.message}")
                } finally {
                    latch.countDown()
                }
            }
            override fun onServiceDisconnected(profile: Int) { latch.countDown() }
        }

        val requested = runCatching {
            adapter.getProfileProxy(context, listener, BluetoothProfile.A2DP)
        }.getOrDefault(false)

        if (!requested) return CodecInfo(RealCodec.UNAVAILABLE, 0, 0)

        val completed = runCatching { latch.await(PROXY_TIMEOUT_MS, TimeUnit.MILLISECONDS) }
            .getOrDefault(false)
        if (!completed) Log.w(TAG, "Timeout esperando proxy A2DP (${PROXY_TIMEOUT_MS}ms)")

        // Liberar el proxy siempre — API de Android exige closeProfileProxy
        // explícito o se filtra el binder del servicio Bluetooth.
        runCatching { proxyRef?.let { adapter.closeProfileProxy(BluetoothProfile.A2DP, it) } }

        return result.get()
    }

    private fun mapCodecConfig(cfg: BluetoothCodecConfig?): CodecInfo {
        if (cfg == null) return CodecInfo(RealCodec.UNKNOWN, 0, 0)
        val sampleRate = decodeSampleRateMask(cfg.sampleRate)
        val bits = decodeBitsMask(cfg.bitsPerSample)
        val codec = when (cfg.codecType) {
            BluetoothCodecConfig.SOURCE_CODEC_TYPE_SBC -> RealCodec.SBC
            BluetoothCodecConfig.SOURCE_CODEC_TYPE_AAC -> RealCodec.AAC
            BluetoothCodecConfig.SOURCE_CODEC_TYPE_APTX -> RealCodec.APTX
            BluetoothCodecConfig.SOURCE_CODEC_TYPE_APTX_HD -> RealCodec.APTX_HD
            BluetoothCodecConfig.SOURCE_CODEC_TYPE_LDAC -> RealCodec.LDAC
            else -> mapExtendedCodec(cfg.codecType)
        }
        return CodecInfo(codec, sampleRate, bits)
    }

    // SOURCE_CODEC_TYPE_OPUS y aptX Adaptive no tienen constante estable en
    // todas las versiones del SDK público — se resuelven por valor entero
    // documentado por Android (Opus=6 desde API 33) en vez de referenciar
    // un símbolo que no compila en minSdk/compileSdk anteriores.
    private fun mapExtendedCodec(codecType: Int): RealCodec = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && codecType == 6 -> RealCodec.OPUS
        codecType == 5 -> RealCodec.APTX_ADAPTIVE // valor de fabricante extendido, no siempre presente
        else -> RealCodec.UNKNOWN
    }

    // BluetoothCodecConfig.sampleRate/bitsPerSample son BITMASKS de
    // capacidades, no un valor único — cuando vienen de getCodecStatus()
    // (config ACTIVA, no lista de capacidades) normalmente un solo bit
    // está encendido; se decodifica el bit más significativo presente.
    private fun decodeSampleRateMask(mask: Int): Int = when {
        mask and BluetoothCodecConfig.SAMPLE_RATE_96000 != 0 -> 96000
        mask and BluetoothCodecConfig.SAMPLE_RATE_88200 != 0 -> 88200
        mask and BluetoothCodecConfig.SAMPLE_RATE_48000 != 0 -> 48000
        mask and BluetoothCodecConfig.SAMPLE_RATE_44100 != 0 -> 44100
        else -> 0
    }

    private fun decodeBitsMask(mask: Int): Int = when {
        mask and BluetoothCodecConfig.BITS_PER_SAMPLE_32 != 0 -> 32
        mask and BluetoothCodecConfig.BITS_PER_SAMPLE_24 != 0 -> 24
        mask and BluetoothCodecConfig.BITS_PER_SAMPLE_16 != 0 -> 16
        else -> 0
    }
}
