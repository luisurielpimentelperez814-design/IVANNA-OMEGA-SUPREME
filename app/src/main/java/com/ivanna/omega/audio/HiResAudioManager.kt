package com.ivanna.omega.audio
import android.content.Context
import android.content.SharedPreferences
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ivanna.omega.magisk.OmegaEngineBridge
import org.json.JSONObject
import java.util.concurrent.Executors
/** Cierra el flujo UI -> persistencia -> daemon. Techo real por ruta:
 *  USB-DAC 384k/32b directo, Bluetooth segun codec (LDAC<=96k/24b), bocina via mixer.
 *
 *  HONESTIDAD DEL CONTRATO (2026-09-21): antes apply() devolvia el resultado del JNI
 *  que escribe /data/adb/ivanna_omega/hires.conf. Un proceso de app SIN root no puede
 *  escribir ahi, asi que devolvia false siempre y la UI nunca confirmaba nada; el
 *  comando al daemon iba por reflexion con un String a una funcion que recibe
 *  JSONObject (IllegalArgumentException tragada) y con una accion ("set_hires")
 *  que el daemon no conocia. Ahora: (1) la seleccion se persiste SIEMPRE en prefs,
 *  (2) el daemon (root) recibe la accion SET_HIRES y es quien escribe hires.conf,
 *  (3) el resultado REAL (respuesta del daemon) se informa por callback. */
object HiResAudioManager {
    private const val TAG = "HiResAudioManager"
    private const val PREFS = "ivanna_hires"
    private const val KEY_RATE = "rate"
    private const val KEY_DEPTH = "depth"
    const val DEFAULT_RATE = 48000
    const val DEFAULT_DEPTH = 24
    // Familia 48 kHz y familia 44.1 kHz. La de 44.1k faltaba (2026-09-20):
    // UsbAudioProManager ya la negocia con el DAC, pero el panel no la
    // ofrecia, asi que toda la musica de 44.1 kHz salia remuestreada a 48k.
    val VALID_RATES = intArrayOf(44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000)
    val VALID_DEPTHS = intArrayOf(16, 24, 32)
    @Volatile var currentRate: Int = DEFAULT_RATE; private set
    @Volatile var currentDepth: Int = DEFAULT_DEPTH; private set
    private val io = Executors.newSingleThreadExecutor { r -> Thread(r, "IvannaHiResIO").apply { isDaemon = true } }
    private val main = Handler(Looper.getMainLooper())
    private fun prefs(ctx: Context): SharedPreferences = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** Carga lo persistido en currentRate/currentDepth SIN tocar daemon ni archivos:
     *  para que la pantalla muestre la eleccion real tras reiniciar el proceso. */
    fun loadPersisted(context: Context) {
        runCatching {
            val p = prefs(context)
            val r = p.getInt(KEY_RATE, currentRate); val d = p.getInt(KEY_DEPTH, currentDepth)
            if (r in VALID_RATES) currentRate = r
            if (d in VALID_DEPTHS) currentDepth = d
        }
    }

    /** Relee lo persistido. Si el usuario NUNCA eligio nada no toca ni el daemon ni el
     *  hires.conf existente (antes reescribia el default 48k/24 encima en cada arranque). */
    fun restore(context: Context) {
        runCatching {
            val p = prefs(context)
            if (!p.contains(KEY_RATE) && !p.contains(KEY_DEPTH)) return
            val r = p.getInt(KEY_RATE, DEFAULT_RATE); val d = p.getInt(KEY_DEPTH, DEFAULT_DEPTH)
            if (r in VALID_RATES && d in VALID_DEPTHS) apply(context, r, d, persist = false)
        }.onFailure { Log.w(TAG, "restore: ${it.message}") }
    }

    /**
     * Devuelve true si los valores son validos y quedaron guardados. NO significa
     * "aplicado al audio": eso lo dice [onResult] (hilo principal), con el resultado
     * real del daemon.
     */
    fun apply(context: Context, rate: Int, depth: Int, persist: Boolean = true,
              onResult: ((String) -> Unit)? = null): Boolean {
        if (rate !in VALID_RATES || depth !in VALID_DEPTHS) { Log.w(TAG, "rate/depth invalidos: $rate/$depth"); return false }
        currentRate = rate; currentDepth = depth
        if (persist) prefs(context).edit().putInt(KEY_RATE, rate).putInt(KEY_DEPTH, depth).apply()
        val appCtx = context.applicationContext
        io.execute {
            val msg = pushToDaemon(rate, depth)
            runCatching { AudioPipeline.syncHardwareSampleRate(appCtx) }
            if (onResult != null) main.post { onResult(msg) }
        }
        return true
    }

    /** Corre en hilo de fondo: socket al daemon con timeouts de segundos. */
    private fun pushToDaemon(rate: Int, depth: Int): String {
        val resp = runCatching {
            OmegaEngineBridge.requestCommand(JSONObject().apply {
                put("action", "SET_HIRES"); put("rate", rate); put("depth", depth)
            })
        }.onFailure { Log.w(TAG, "SET_HIRES: ${it.message}") }.getOrNull()
        if (resp != null && resp.optBoolean("ok", false)) {
            return "Guardado en el daemon: se aplica al reiniciar el daemon (Magisk)."
        }
        // Sin daemon: intento directo (solo funciona si el proceso tiene permiso, p.ej. root).
        val direct = runCatching {
            com.ivanna.omega.core.IvannaNativeLib.nativeSetSampleRate(rate) &&
            com.ivanna.omega.core.IvannaNativeLib.nativeSetBitDepth(depth)
        }.getOrDefault(false)
        return if (direct) "Guardado en hires.conf: se aplica al reiniciar el daemon."
        else "Guardado en la app, pero el daemon (Magisk/root) no respondio: la seleccion no llego al motor."
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
