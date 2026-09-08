package com.ivanna.omega.neuromorphic

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeLibraryLoader

/**
 * Kotlin bridge for PI-LSTM Milenio v2.0 (from IVANNA-ULTRA).
 * Signal path: Input (96kHz) → 4x Upsample → CT-LSTM RK4 → HRTF → 4x Downsample → Output
 */
object PiLstmBridge {
    private const val TAG = "IVANNA_OMEGA_LSTM"
    private var ready = false

    init {
        try {
            if (NativeLibraryLoader.ensureLoaded()) {
                nativeInit()
                ready = true
                Log.i(TAG, "PI-LSTM Milenio initialized")
            }
        } catch (t: Throwable) {
            ready = false
            Log.e(TAG, "PI-LSTM init failed", t)
        }
    }

    val isReady: Boolean get() = ready

    fun setAlpha(v: Float)        { if (ready) nativeSetAlpha(v) }
    fun setBeta(v: Float)         { if (ready) nativeSetBeta(v) }
    fun setGamma(v: Float)        { if (ready) nativeSetGamma(v) }
    fun setDelta(v: Float)        { if (ready) nativeSetDelta(v) }
    private var lastHarmonicGain = 0.2f
    fun setHarmonicGain(v: Float) { if (ready) { lastHarmonicGain = v; nativeSetHarmonicGain(v) } }
    fun setHrtfEnabled(en: Boolean) { if (ready) nativeSetHrtfEnabled(en) }
    fun getNpSat(): Float = if (ready) nativeGetNpSat() else 0f
    fun getError(): Float = if (ready) nativeGetError() else 0f

    // FIX (huérfano JNI): pi_lstm_bridge_jni.cpp:223 implementa
    // Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeResetTelemetry
    // (reset de g_residual_ema + g_prev_h/g_prev_c/g_prev_ns) pero ningún
    // external fun lo exponía — 0 callers posibles desde Kotlin. El propio
    // comentario C++ (líneas 218-221) documenta el caso de uso: al cambiar
    // de pista o reinicializar el motor, la EMA del residual arrastra el
    // transitorio anterior y contamina getError(). Se expone como wrapper
    // público con guard de ready, idéntico patrón al resto del objeto.
    fun resetTelemetry() { if (ready) nativeResetTelemetry() }

    private external fun nativeInit()
    private external fun nativeSetAlpha(v: Float)
    private external fun nativeSetBeta(v: Float)
    private external fun nativeSetGamma(v: Float)
    private external fun nativeSetDelta(v: Float)
    private external fun nativeSetHarmonicGain(v: Float)
    private external fun nativeSetHrtfEnabled(en: Boolean)
    private external fun nativeGetNpSat(): Float
    private external fun nativeGetError(): Float
    private external fun nativeResetTelemetry()

    // === NUEVOS PARÁMETROS NEURO-COCHLEAR ===
    // ── NPE completo ────────────────────────────────────────────────
    /**
     * FIX (semántica cruzada): antes enrutaba el master gain (dB) a
     * nativeSetEta, que es el AMORTIGUAMIENTO η de la ODE (rango 0..5) —
     * el slider de ganancia maestra estaba deformando la dinámica del
     * integrador, no el volumen. El motor PI-LSTM no expone ganancia de
     * salida; la ruta honesta es el escalado de salida del player
     * (mismo mecanismo que setAgc): gain_lin = 10^(dB/20).
     * η queda expuesto con su nombre real en setOdeDamping().
     */
    fun setMasterGain(db: Float) {
        if (!ready) return
        val safeDb = if (db.isFinite()) db.coerceIn(-18f, 18f) else 0f
        val gainLin = Math.pow(10.0, safeDb / 20.0).toFloat()
        val inst = com.ivanna.omega.audio.IvannaBridgePlayer.activeInstance
        if (inst != null) {
            runCatching { inst.updateNpeKotlinParams(outputScaling = gainLin) }
                .onFailure { Log.w(TAG, "setMasterGain updateNpe: ${it.message}") }
        }
        lastMasterGainLin = gainLin
    }

    /** Amortiguamiento η de la ODE (rango 0..5) — su propósito REAL. */
    fun setOdeDamping(eta: Float) { if (ready) nativeSetEta(eta.coerceIn(0f, 5f)) }

    /** Techo neuroplástico NP_max (rango 0.1..10) — invalida la semilla residual. */
    fun setNeuroplasticityMax(npMax: Float) { if (ready) nativeSetNPMax(npMax.coerceIn(0.1f, 10f)) }
    fun setAgc(targetDb: Float, rate: Float) {
        if (!ready) return
        try {
            val safeTarget = if (targetDb.isFinite()) targetDb.coerceIn(-36f, 0f) else -18f
            val safeRate   = if (rate.isFinite()) rate.coerceIn(0f, 1f) else 0.5f
            val gain = (safeTarget / -36f).coerceIn(0f, 1f)
            val inst = com.ivanna.omega.audio.IvannaBridgePlayer.activeInstance
            if (inst != null) {
                runCatching { inst.updateNpeKotlinParams(outputScaling = gain * 0.5f) }
                    .onFailure {
                        android.util.Log.w(TAG, "AGC updateNpe: ${it.message}")
                    }
            }
        } catch (t: Throwable) {
            android.util.Log.e("IVANNA_OMEGA_LSTM", "setAgc safe fallback error", t)
        }
    }

    fun setAdaptEnabled(en: Boolean) {
        if (ready) IvannaNativeLib.nativeSetAdaptEnabled(en)
    }
    fun setCochlearEnabled(en: Boolean) {
        // Cochlear → spatial wet: on=1.0, off=0.0
        if (ready) IvannaNativeLib.nativeSetSpatialWet(if (en) 1f else 0f)
    }
    // Estado para restaurar tras bypass (FIX: antes harmonicGain se ponía a 0
    // al entrar en bypass y JAMÁS se restauraba al salir — el ajuste del
    // usuario se perdía en silencio tras un ciclo bypass on→off).
    private var savedHarmonicGain = 0.2f
    private var lastMasterGainLin = 1.0f

    fun setBypass(bypass: Boolean) {
        // Bypass NPE: deshabilita motor adaptativo y fuerza ganancia neutra
        if (ready) {
            IvannaNativeLib.nativeSetAdaptEnabled(!bypass)
            if (bypass) {
                savedHarmonicGain = lastHarmonicGain
                nativeSetHarmonicGain(0f)
            } else {
                nativeSetHarmonicGain(savedHarmonicGain)
            }
        }
    }

    private external fun nativeSetEta(v: Float)
    private external fun nativeSetNPMax(v: Float)

    fun setClarity(clarity: Float) {
        // FIX (UnsatisfiedLinkError latente): llamaba external fun sin guard
        // 'ready' — crasheaba si la librería nativa no cargó.
        if (!ready) return
        // Mapear claridad (0-1) a ganancia de armónicos y lateral inhibition
        val c = clarity.coerceIn(0f, 1f)
        setHarmonicGain(0.1f + c * 0.8f)
        nativeSetBeta(0.2f + c * 0.6f)
    }

    fun setWarmth(warmth: Float) {
        // FIX: mismo guard 'ready' que el resto del objeto.
        if (!ready) return
        // Mapear calidez (0-1) a compresión OHC y gamma
        val w = warmth.coerceIn(0f, 1f)
        val ohcComp = 0.1f + w * 0.7f
        nativeSetGamma(0.5f + w * 0.5f)
        // Nota: nativeSetOhcCompression se añadiría en C++ si existiera,
        // pero usamos nativeSetAlpha como proxy (ajuste de ganancia maestra)
        nativeSetAlpha(ohcComp)
    }
}
