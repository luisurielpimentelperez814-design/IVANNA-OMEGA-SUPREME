package com.ivanna.omega.audio
import android.content.Context
import android.content.SharedPreferences
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.util.Log
/** Cierra el flujo UI -> persistencia -> nativo -> daemon. Techo real por ruta:
 *  USB-DAC 384k/32b directo, Bluetooth segun codec (LDAC<=96k/24b), bocina via mixer. */
object HiResAudioManager {
    private const val TAG = "HiResAudioManager"
    private const val PREFS = "ivanna_hires"
    private const val KEY_RATE = "rate"
    private const val KEY_DEPTH = "depth"
    const val DEFAULT_RATE = 48000
    const val DEFAULT_DEPTH = 24
    val VALID_RATES = intArrayOf(48000, 96000, 192000, 384000)
    val VALID_DEPTHS = intArrayOf(16, 24, 32)
    @Volatile var currentRate: Int = DEFAULT_RATE; private set
    @Volatile var currentDepth: Int = DEFAULT_DEPTH; private set
    private fun prefs(ctx: Context): SharedPreferences = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    fun restore(context: Context) {
        runCatching {
            val p = prefs(context); val r = p.getInt(KEY_RATE, DEFAULT_RATE); val d = p.getInt(KEY_DEPTH, DEFAULT_DEPTH)
            if (r in VALID_RATES && d in VALID_DEPTHS) apply(context, r, d, persist = false)
        }.onFailure { Log.w(TAG, "restore: ${it.message}") }
    }
    fun apply(context: Context, rate: Int, depth: Int, persist: Boolean = true): Boolean {
        if (rate !in VALID_RATES || depth !in VALID_DEPTHS) { Log.w(TAG, "rate/depth invalidos: $rate/$depth"); return false }
        currentRate = rate; currentDepth = depth
        if (persist) prefs(context).edit().putInt(KEY_RATE, rate).putInt(KEY_DEPTH, depth).apply()
        val nativeOk = runCatching {
            com.ivanna.omega.core.IvannaNativeLib.nativeSetSampleRate(rate) &&
            com.ivanna.omega.core.IvannaNativeLib.nativeSetBitDepth(depth)
        }.onFailure { Log.w(TAG, "JNI hi-res no disponible: ${it.message}") }.getOrDefault(false)
        runCatching {
            val cls = Class.forName("com.ivanna.omega.magisk.OmegaEngineBridge")
            val inst = cls.getField("INSTANCE").get(null)
            cls.methods.firstOrNull { it.name == "sendCommand" && it.parameterCount == 1 }
                ?.invoke(inst, "{\"cmd\":\"set_hires\",\"rate\":$rate,\"depth\":$depth}")
        }
        runCatching { AudioPipeline.syncHardwareSampleRate(context) }
        return nativeOk
    }
    fun activeRoute(context: Context): Pair<String, Int> {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return ("Desconocida" to 48000)
        val devs = runCatching { am.getDevices(AudioManager.GET_DEVICES_OUTPUTS) }.getOrNull() ?: return ("Desconocida" to 48000)
        var best = ("Bocina interna (mixer Android re-muestrea)" to 48000)
        for (d in devs) when (d.type) {
            AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET -> return ("USB-DAC — salida directa bit-perfect" to 384000)
            AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> best = ("Bluetooth A2DP — techo del codec (LDAC <= 96 kHz/24-bit)" to 96000)
        }
        return best
    }
}
