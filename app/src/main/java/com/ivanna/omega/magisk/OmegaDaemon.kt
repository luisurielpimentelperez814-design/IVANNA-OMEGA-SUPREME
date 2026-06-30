package com.ivanna.omega.magisk

import android.util.Log

/**
 * IVANNA-OMEGA-SUPREME — OmegaDaemon JNI Bridge
 *
 * Ciclo de vida:
 *   OmegaDaemon.start()  → lanza process_audio_thread + socket_server_thread en C++
 *   OmegaDaemon.stop()   → join limpio de ambos hilos + libera SHM
 *
 * PF Engine (tanh drive → Biquad 4-band EQ → mix):
 *   setPFParams(...)  → bulk setter, un solo bump de coeff_version
 *   setPFDrive(...)   → individual (no recomputa coeficientes Biquad)
 *   setPFFreq(...)    → sí recomputa coeficientes (bump de versión)
 *   getPFParams()     → FloatArray[13] para sincronizar la UI
 *
 * Telemetría:
 *   getTemperature()  → °C desde /sys/class/power_supply/battery/temp
 *   getLatency()      → ms del último bloque procesado
 */
object OmegaDaemon {

    private const val TAG = "OmegaDaemon"
    private var loaded = false

    init {
        try {
            System.loadLibrary("ivanna_omega")
            loaded = true
            Log.i(TAG, "libivanna_omega cargada")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "libivanna_omega no disponible: ${e.message}")
        }
    }

    val isLoaded: Boolean get() = loaded

    // ── Ciclo de vida ─────────────────────────────────────────────────────────
    fun start(): Boolean = if (loaded) nativeStart() else false
    fun stop() { if (loaded) nativeStop() }

    // ── Control básico ────────────────────────────────────────────────────────
    fun setProcessing(enabled: Boolean) { if (loaded) nativeSetProcessing(enabled) }
    fun setIntensity(v: Float)          { if (loaded) nativeSetIntensity(v) }

    // ── Telemetría ────────────────────────────────────────────────────────────
    fun getTemperature(): Float = if (loaded) nativeGetTemperature() else 0f
    fun getLatency(): Float     = if (loaded) nativeGetLatency()     else 0f

    // ── PF Engine — bulk setter (ruta preferida desde la UI) ─────────────────
    /**
     * Aplica los 13 parámetros del PF Engine en una sola llamada JNI.
     * El daemon hace un único bump de pf_param_version → recomputación Biquad
     * en el siguiente bloque de audio.
     */
    fun setPFParams(
        drive: Float, wet: Float, mix: Float,
        alpha: Float, beta: Float, gamma: Float,
        freq: Float, resonance: Float,
        low: Float, mid: Float, high: Float,
        presence: Float, master: Float
    ) {
        if (!loaded) return
        nativeSetPFParams(drive, wet, mix, alpha, beta, gamma, freq, resonance,
                          low, mid, high, presence, master)
    }

    /**
     * Devuelve FloatArray[13]:
     * [drive, wet, mix, alpha, beta, gamma, freq, resonance, low, mid, high, presence, master]
     */
    fun getPFParams(): FloatArray = if (loaded) nativeGetPFParams() else FloatArray(13)

    // ── PF Engine — setters individuales ─────────────────────────────────────
    // Sin recomputación de Biquad (solo escalares en el hot-path)
    fun setPFDrive(v: Float)   { if (loaded) nativeSetPFDrive(v) }
    fun setPFWet(v: Float)     { if (loaded) nativeSetPFWet(v) }
    fun setPFMix(v: Float)     { if (loaded) nativeSetPFMix(v) }
    fun setPFAlpha(v: Float)   { if (loaded) nativeSetPFAlpha(v) }
    fun setPFBeta(v: Float)    { if (loaded) nativeSetPFBeta(v) }
    fun setPFGamma(v: Float)   { if (loaded) nativeSetPFGamma(v) }
    fun setPFMaster(v: Float)  { if (loaded) nativeSetPFMaster(v) }

    // Con recomputación de coeficientes Biquad (bump de pf_param_version)
    fun setPFFreq(v: Float)      { if (loaded) nativeSetPFFreq(v) }
    fun setPFResonance(v: Float) { if (loaded) nativeSetPFResonance(v) }
    fun setPFLow(v: Float)       { if (loaded) nativeSetPFLow(v) }
    fun setPFMid(v: Float)       { if (loaded) nativeSetPFMid(v) }
    fun setPFHigh(v: Float)      { if (loaded) nativeSetPFHigh(v) }
    fun setPFPresence(v: Float)  { if (loaded) nativeSetPFPresence(v) }

    // ── Declaraciones JNI ─────────────────────────────────────────────────────
    private external fun nativeStart(): Boolean
    private external fun nativeStop()
    private external fun nativeSetProcessing(v: Boolean)
    private external fun nativeSetIntensity(v: Float)
    private external fun nativeGetTemperature(): Float
    private external fun nativeGetLatency(): Float

    private external fun nativeSetPFParams(
        drive: Float, wet: Float, mix: Float,
        alpha: Float, beta: Float, gamma: Float,
        freq: Float, resonance: Float,
        low: Float, mid: Float, high: Float,
        presence: Float, master: Float
    )
    private external fun nativeGetPFParams(): FloatArray

    private external fun nativeSetPFDrive(v: Float)
    private external fun nativeSetPFWet(v: Float)
    private external fun nativeSetPFMix(v: Float)
    private external fun nativeSetPFAlpha(v: Float)
    private external fun nativeSetPFBeta(v: Float)
    private external fun nativeSetPFGamma(v: Float)
    private external fun nativeSetPFMaster(v: Float)
    private external fun nativeSetPFFreq(v: Float)
    private external fun nativeSetPFResonance(v: Float)
    private external fun nativeSetPFLow(v: Float)
    private external fun nativeSetPFMid(v: Float)
    private external fun nativeSetPFHigh(v: Float)
    private external fun nativeSetPFPresence(v: Float)
}
