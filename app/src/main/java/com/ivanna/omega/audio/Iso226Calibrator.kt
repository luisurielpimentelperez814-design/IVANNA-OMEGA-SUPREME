package com.ivanna.omega.audio

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPStatePrefs
import com.ivanna.omega.magisk.OmegaEngineBridge
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.exp
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow

/**
 * Iso226Calibrator — Calibración ISO 226:2003 completa.
 *
 * Implementa la tabla oficial (Annex A) con las 29 frecuencias:
 *   αf  — exponente de percepción de loudness
 *   Lu  — magnitud función transferencia lineal (norm. a 1 kHz)
 *   Tf  — umbral de audición en silencio (dB SPL)
 *
 * Fórmula (§A.1):
 *   Af  = 4.47×10⁻³ × (10^(0.025×Ln) − 1.15) + (0.4 × 10^((Tf+Lu)/10 − 9))^αf
 *   Lp  = (10/αf) × log10(Af) − Lu + 94
 *
 * Compensación:
 *   ΔEQ(f) = Lp(f, refPhon) − Lp(f, listenPhon)
 *
 * La curva resultante se aplica en tres capas:
 *   1. Android Equalizer (AudioEffect) → todas las apps del sistema
 *   2. DSPBridge (libivanna_omega.so) → reproductor propio
 *   3. OmegaEngineBridge socket → daemon Magisk system-wide
 */
object Iso226Calibrator {

    private const val TAG = "Iso226Calibrator"

    // ── Tabla ISO 226:2003 — 29 frecuencias ──────────────────────────────────
    private val FREQS = floatArrayOf(
        20f, 25f, 31.5f, 40f, 50f, 63f, 80f, 100f, 125f, 160f,
        200f, 250f, 315f, 400f, 500f, 630f, 800f, 1000f, 1250f, 1600f,
        2000f, 2500f, 3150f, 4000f, 5000f, 6300f, 8000f, 10000f, 12500f
    )

    /** αf — exponente de percepción de loudness */
    private val AF = floatArrayOf(
        0.532f, 0.506f, 0.480f, 0.455f, 0.432f, 0.409f, 0.387f, 0.367f, 0.349f, 0.330f,
        0.315f, 0.301f, 0.288f, 0.276f, 0.267f, 0.259f, 0.253f, 0.250f, 0.246f, 0.244f,
        0.243f, 0.243f, 0.243f, 0.242f, 0.242f, 0.245f, 0.254f, 0.271f, 0.301f
    )

    /** Lu — magnitud función transferencia lineal normalizada a 1 kHz */
    private val LU = floatArrayOf(
        -31.6f, -27.2f, -23.0f, -19.1f, -15.9f, -13.0f, -10.3f, -8.1f, -6.2f, -4.5f,
        -3.1f, -2.0f, -1.1f, -0.4f, 0.0f, 0.3f, 0.5f, 0.0f, -2.7f, -4.1f,
        -1.0f, 1.7f, 2.5f, 1.2f, -2.1f, -7.1f, -11.2f, -10.7f, -3.1f
    )

    /** Tf — umbral de audición en silencio (dB SPL) */
    private val TF = floatArrayOf(
        78.5f, 68.7f, 59.5f, 51.1f, 44.0f, 37.5f, 31.5f, 26.5f, 22.1f, 17.9f,
        14.4f, 11.4f, 8.6f, 6.2f, 4.4f, 3.0f, 2.2f, 2.4f, 3.5f, 1.7f,
        -1.3f, -4.2f, -6.0f, -5.4f, -1.5f, 6.0f, 12.6f, 13.9f, 12.3f
    )

    // ── Las 10 bandas del Android Equalizer → índice en la tabla ISO 226 ─────
    // Frecuencias: 31.5/63/125/250/500/1k/2k/4k/8k/16k Hz
    // 16k Hz no está en ISO 226 (max=12.5k) → usamos 12.5k como aproximación
    private val EQ_BAND_TO_ISO_IDX = intArrayOf(2, 5, 8, 11, 14, 17, 20, 23, 26, 28)
    private val EQ_BAND_FREQS      = floatArrayOf(31.5f, 63f, 125f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 12500f)

    // ── Calibración en curso ──────────────────────────────────────────────────
    @Volatile var isCalibrated   = false; private set
    @Volatile var listenPhon     = 60f;   private set
    @Volatile var refPhon        = 80f;   private set
    @Volatile var lastGainsDsp   = FloatArray(10); private set

    // ── Fórmula ISO 226:2003 §A.1 ────────────────────────────────────────────
    /**
     * Calcula la presión sonora (dB SPL) para una frecuencia (idx en tabla)
     * a un nivel [phon] dado. Phon clampado a [0, 90].
     */
    fun lp(freqIdx: Int, phon: Float): Float {
        val Ln = phon.coerceIn(0f, 90f)
        val af = AF[freqIdx].toDouble()
        val lu = LU[freqIdx].toDouble()
        val tf = TF[freqIdx].toDouble()

        val Af = 4.47e-3 * (10.0.pow(0.025 * Ln) - 1.15) +
                (0.4 * 10.0.pow((tf + lu) / 10.0 - 9.0)).pow(af)

        return ((10.0 / af) * log10(max(Af, 1e-30)) - lu + 94.0).toFloat()
    }

    /**
     * Curva de compensación completa: ΔdB por cada una de las 10 bandas EQ.
     * gain[i] > 0 → boost; < 0 → cut.
     */
    fun computeCompensation(listenPhon: Float, refPhon: Float): FloatArray {
        val raw = FloatArray(10) { i ->
            val idx = EQ_BAND_TO_ISO_IDX[i]
            lp(idx, refPhon) - lp(idx, listenPhon)
        }
        val offset = raw[5]
        for (i in 0 until 10) raw[i] -= offset
        return raw
    }

    // ── Clamp de ganancias para proteger el sistema ───────────────────────────
    // Android Equalizer: ±15 dB máximo típico (limitamos a ±12 dB)
    private fun clampDbForEq(db: Float) = db.coerceIn(-12f, 12f)

    // ── Aplicar al Android Equalizer (todas las apps del sistema) ────────────
    /**
     * Aplica los gains (dB) al Android Equalizer en millibeliqs (dB × 100).
     * Se llama desde IvannaGlobalEffectManager.applyIso226Compensation().
     */
    fun applyToEqualizer(gains: FloatArray, effectManager: IvannaGlobalEffectManager) {
        val millibel = IntArray(10) { i ->
            (clampDbForEq(gains.getOrElse(i) { 0f }) * 100f).toInt()
        }
        val profile = effectManager.currentProfile().copy(eqBands = millibel)
        effectManager.applyProfile(profile)
        Log.i(TAG, "ISO 226 → Equalizer: ${millibel.map { "${it/100f}dB" }}")
    }

    // ── Aplicar al DSPBridge (libivanna_omega.so) ─────────────────────────────
    /**
     * Mapea las 10 bandas ISO 226 a los parámetros del DSPBridge:
     *   low      → promedio de bandas 0..2  (31–125 Hz)
     *   mid      → promedio de bandas 3..5  (250 Hz–1 kHz)
     *   high     → promedio de bandas 6..7  (2–4 kHz)
     *   presence → promedio de bandas 8..9  (8–12.5 kHz)
     */
    fun applyToDSPBridge(context: Context, gains: FloatArray) {
        if (!DSPBridge.isLoaded) return
        // FIX (calibración 15x demasiado débil): se dividía por 15f asumiendo
        // que DSPBridge.setParams() espera valores [0..1]. El comentario canónico
        // en ParametricEQ::setParams() dice explícitamente:
        //   "p.low / p.mid / p.high / p.presence arrive as dB values directly"
        // Con la división, gains[0..2] promedio de +6 dB → 0.4 dB aplicados.
        // Con la corrección, se aplican los dB reales. clampDbForEq() ya limita
        // a ±18 dB, así que no hay riesgo de overflow.
        val low      = clampDbForEq((gains[0] + gains[1] + gains[2]) / 3f)
        val mid      = clampDbForEq((gains[3] + gains[4] + gains[5]) / 3f)
        val high     = clampDbForEq((gains[6] + gains[7]) / 2f)
        val presence = clampDbForEq((gains[8] + gains[9]) / 2f)

        // FIX (2026-08-29 — "al aplicar ISO 226 truena el audio"):
        // La versión anterior PISABA todo el estado DSP con valores hardcodeados:
        //   alpha=0.94  → threshold del compresor = -24 + 0.94*24 = -1.4 dB
        //   beta=0.85   → ratio = 1 + 0.85*19 ≈ 17:1  (un limiter aplastando)
        //   master=st.masterGain (AudioState, LINEAL 0..2) pero GainStage
        //     interpreta master como dB (dbToLin(p.master)) → 1.0 "dB" = ×1.12,
        //     y peor: si el usuario tenía master en dB distinto se perdía.
        // Compresión brutal + EQ boost + master en unidades equivocadas, todo
        // de golpe → trueno inmediato al aplicar.
        //
        // Ahora se preserva el estado actual del usuario (DSPStatePrefs) y
        // solo se SUMA el delta ISO 226 encima de su EQ actual. Los demás
        // parámetros (drive/wet/mix/alpha/beta/gamma/freq/resonance/master)
        // quedan exactamente como el usuario los tenía — cero cambio de
        // carácter, solo la curva de compensación.
        val cur = DSPStatePrefs.load(context)
        DSPBridge.setParams(
            drive = cur.drive, wet = cur.wet, mix = cur.mix,
            alpha = cur.alpha, beta = cur.beta, gamma = cur.gamma,
            freq = cur.freq, resonance = cur.resonance,
            low = low + cur.low, mid = mid + cur.mid, high = high + cur.high,
            presence = presence + cur.presence, master = cur.master
        )
        Log.i(TAG, "ISO 226 → DSPBridge (delta sobre estado actual): low=${"%.2f".format(low)}dB mid=${"%.2f".format(mid)}dB high=${"%.2f".format(high)}dB presence=${"%.2f".format(presence)}dB")
    }

    // ── Aplicar al daemon Magisk vía socket ───────────────────────────────────
    /**
     * Envía SET_EQ_BANDS al daemon omega_daemon_socket con los 10 gains en dB.
     */
    fun applyToOmegaBridge(gains: FloatArray, listenPhon: Float, refPhon: Float): Boolean {
        if (!OmegaEngineBridge.isConnected) return false
        val arr = JSONArray().apply { gains.forEach { put(it.toDouble()) } }
        val ok = OmegaEngineBridge.sendCommand(JSONObject().apply {
            put("action",     "SET_EQ_BANDS")
            put("gains",      arr)
            put("listenPhon", listenPhon.toDouble())
            put("refPhon",    refPhon.toDouble())
            put("timestamp",  System.currentTimeMillis())
        })
        Log.i(TAG, "ISO 226 → OmegaBridge socket: ${if (ok) "OK" else "FAIL (daemon offline)"}")
        return ok
    }

    // ── Aplicar en todas las capas ────────────────────────────────────────────
    /**
     * Punto de entrada principal. Calcula y aplica la compensación ISO 226
     * en el Equalizer nativo, DSPBridge y daemon Magisk.
     *
     * @return CalibrationResult con el estado de cada capa
     */
    fun applyAll(
        context: Context,
        listenPhon: Float,
        refPhon: Float,
        effectManager: IvannaGlobalEffectManager
    ): CalibrationResult {
        val gains = computeCompensation(listenPhon, refPhon)

        var eqOk     = false
        var dspOk    = false
        var socketOk = false

        runCatching {
            applyToEqualizer(gains, effectManager)
            eqOk = true
        }.onFailure { Log.w(TAG, "Equalizer ISO 226 error: ${it.message}") }

        runCatching {
            applyToDSPBridge(context, gains)
            dspOk = DSPBridge.isLoaded
        }.onFailure { Log.w(TAG, "DSPBridge ISO 226 error: ${it.message}") }

        runCatching {
            socketOk = applyToOmegaBridge(gains, listenPhon, refPhon)
        }.onFailure { Log.w(TAG, "Socket ISO 226 error: ${it.message}") }

        this.listenPhon   = listenPhon
        this.refPhon      = refPhon
        this.lastGainsDsp = gains
        this.isCalibrated = eqOk || dspOk || socketOk

        Log.i(TAG, "ISO 226 calibración completada: EQ=$eqOk DSP=$dspOk Socket=$socketOk")
        return CalibrationResult(gains, eqOk, dspOk, socketOk)
    }

    data class CalibrationResult(
        val gains: FloatArray,       // 10 gains en dB
        val eqApplied: Boolean,      // Android Equalizer
        val dspApplied: Boolean,     // DSPBridge nativo
        val socketApplied: Boolean   // Daemon Magisk
    ) {
        val anyApplied get() = eqApplied || dspApplied || socketApplied
        val summary get() = buildString {
            append("EQ=${if (eqApplied) "✅" else "⚪"} ")
            append("DSP=${if (dspApplied) "✅" else "⚪"} ")
            append("Socket=${if (socketApplied) "✅" else "⚪"}")
        }
    }

    /** Resumen de la curva actual en formato logueable */
    fun describe(): String = if (!isCalibrated) "No calibrado"
    else "${listenPhon.toInt()}→${refPhon.toInt()} Phon | " +
            EQ_BAND_FREQS.zip(lastGainsDsp.toList())
                .joinToString(" ") { (f, g) -> "${if (f >= 1000) "${(f/1000).toInt()}k" else "${f.toInt()}"}:${"%+.1f".format(g)}" }

    // ── Persistencia de calibración ISO 226 ──────────────────────────────────
    private const val PREFS_NAME   = "ivanna_iso226"
    private const val KEY_LISTEN   = "listen_phon"
    private const val KEY_REF      = "ref_phon"
    private const val KEY_CALIBRATED = "calibrated"

    /**
     * Persiste la calibración actual en SharedPreferences.
     * Llamar después de applyAll() para que sobreviva reinicios.
     */
    fun persist(context: Context) {
        if (!isCalibrated) return
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putFloat(KEY_LISTEN, listenPhon)
            .putFloat(KEY_REF, refPhon)
            .putBoolean(KEY_CALIBRATED, true)
            .apply()
        Log.d(TAG, "ISO 226 calibración persistida: ${listenPhon}→${refPhon} Phon")
    }

    /**
     * Restaura la calibración desde SharedPreferences y la aplica.
     * Llamar desde IVANNAApplication.onCreate() DESPUÉS de que
     * IvannaGlobalEffectManager esté listo.
     *
     * FIX: Sin esto la calibración ISO 226 se perdía en cada reinicio de app.
     * El usuario calibraba una vez y al cerrar/abrir perdía el ajuste.
     *
     * @return true si había calibración guardada y se aplicó correctamente.
     */
    fun restoreIfSaved(context: Context, effectManager: IvannaGlobalEffectManager): Boolean {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        if (!prefs.getBoolean(KEY_CALIBRATED, false)) return false
        val savedListen = prefs.getFloat(KEY_LISTEN, 60f)
        val savedRef    = prefs.getFloat(KEY_REF, 80f)
        Log.i(TAG, "Restaurando calibración ISO 226: ${savedListen}→${savedRef} Phon")
        val result = applyAll(context, savedListen, savedRef, effectManager)
        return result.anyApplied
    }

}
