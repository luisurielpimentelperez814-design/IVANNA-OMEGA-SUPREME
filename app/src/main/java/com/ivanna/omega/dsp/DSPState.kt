package com.ivanna.omega.dsp

import com.ivanna.omega.core.IVANNAApplication
import kotlinx.coroutines.launch

/**
 * DSP State — inmutable data class para todos los parámetros DSP.
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */
data class DSPState(
    // Core parameters — TUNED v3.3 (alineados con dsp_types.h + ParameterStore)
    val drive: Float     = 0.45f,  // TUNED v3.3: 0.65→0.45 (drive 10.75x→7.75x, menos distorsion)
    val wet: Float       = 0.32f,  // TUNED v3.3: 0.50→0.32 (exciter wet 50%→32%, mas sutil)
    val mix: Float       = 0.70f,
    val alpha: Float     = 0.375f, // TUNED v3.3: 0.50→0.375 (comp threshold -12→-15 dB)
    val beta: Float      = 0.105f, // TUNED v3.3: 0.50→0.105 (ratio 10.5:1→3.0:1, musical)
    val gamma: Float     = 0.72f,  // TUNED v3.3: 0.50→0.72 (attack 31ms, release 176ms)
    val freq: Float      = 1000f,
    val resonance: Float = 0.707f,

    // EQ gains (dB)
    val low: Float      = 0.0f,
    val mid: Float      = 0.0f,
    val high: Float     = 0.0f,
    val presence: Float = 0.0f,
    val master: Float   = 0.0f,

    // Compressor
    val compThreshold: Float = -18.0f,
    val compRatio: Float     =   4.0f,

    // Exciter
    val exciterDrive: Float = 0.3f,

    // Stereo
    val stereoWidth: Float = 1.0f,
    val makeupGain: Float  = 0.0f,

    // Bypass
    val bypass: Boolean = false
) {
    /** EQ gains array para JNI */
    val eqGains: FloatArray
        get() = floatArrayOf(low, mid, high, presence)

    /**
     * Envía todos los parámetros al DSP nativo vía DSPBridge.
     *
     * FIX (tuning magistral): stereoWidth nunca se enviaba — nativeSetParams
     * no lo incluía en su firma. Ahora va por un canal JNI dedicado
     * (nativeSetStereoWidth), separado de gamma (que sólo controla timing
     * del compresor).
     *
     * FIX (auditoría del módulo Magisk — hallazgo crítico): IVANNAApplication.
     * omegaBridge (el cliente socket hacia el daemon system-wide) sólo se
     * usaba para connect()/disconnect() — ningún control de la UI le llegaba
     * nunca. El daemon procesaba TODAS las apps del sistema con los valores
     * por defecto de OmegaSharedState congelados desde el arranque, sordo a
     * cualquier ajuste. Ahora pushToNative() —el único punto de entrada real
     * de cambios de parámetros en toda la app— también espeja hacia el
     * daemon. setPFParams() de OmegaEngineBridge ya maneja reconexión/no-op
     * silencioso si el daemon no está disponible (modo no-root), así que
     * esta llamada es segura incondicionalmente.
     */
    fun pushToNative() {
        DSPBridge.setParams(
            drive, wet, mix,
            alpha, beta, gamma,
            freq, resonance,
            low, mid, high, presence, master
        )
        DSPBridge.setStereoWidth(stereoWidth)

        // FIX CRÍTICO (causa real de trabazón/crash al mover sliders):
        // omegaBridge.setPFParams() hace I/O de socket Unix síncrono (13
        // comandos de texto en serie, cada uno con su propio write()+flush(),
        // y potencialmente un connect() bloqueante si la conexión se cayó).
        // pushToNative() se llama en CADA tick de CUALQUIER slider del DSP
        // (onExciterChange/onEqChange/onWidthChange/onCompThresholdChange/
        // etc.), disparado desde Compose en el hilo principal — eso
        // significa que se estaba haciendo I/O de red síncrono en el hilo
        // de UI decenas de veces por segundo mientras se arrastraba un
        // slider. Combinado con el bug del lado del daemon (que cerraba la
        // conexión tras cada comando, forzando reconexión en cada uno de
        // los 13 — ver fix en omega_daemon.cpp/socketLoop), esto producía
        // ANR/trabazón real y, en carga sostenida, el sistema mataba el
        // proceso. Con el daemon ya arreglado esto es mucho más rápido,
        // pero I/O de red síncrono en el hilo principal sigue siendo
        // arquitectónicamente incorrecto — se despacha al appScope (IO)
        // igual que se hizo con LearningBias.captureCorrection().
        // Actualizar AudioEffect sessions (Spotify/YouTube/etc) con sliders actuales.
        // mid == EQ global dB (la UI setea low=mid=high=presence al mismo valor).
        // alpha/beta → compressor threshold/ratio con la misma formula que el C++.
        globalEffectManager?.adjustLiveParams(
            eqGainDb        = mid,
            stereoWidth     = stereoWidth,
            compThresholdDb = -24f + alpha * 24f,
            compRatio       = 1f + beta * 19f
        )

        // FIX (crash): trySend es non-blocking y O(1).
        // Channel(CONFLATED) en IVANNAApplication descarta valores intermedios
        // — solo el ultimo llega al socket. Elimina la acumulacion de coroutines
        // bloqueadas (CONNECT_TIMEOUT_MS=2000ms x 60fps = OOM en minutos).
        IVANNAApplication.pfParamChannel.trySend(
            floatArrayOf(drive, wet, mix, alpha, beta, gamma,
                         freq, resonance, low, mid, high, presence, master)
        )
    }

    companion object {
        /**
         * Referencia a IvannaGlobalEffectManager seteada por IVANNAApplication.
         * Permite que pushToNative() actualice las sesiones de AudioEffect
         * (Spotify, YouTube, etc.) sin necesitar un Context.
         */
        @Volatile
        var globalEffectManager: com.ivanna.omega.audio.IvannaGlobalEffectManager? = null

        // ── Campos estáticos del PF Engine (escritos desde AudioEngine) ──────
        @JvmField var pfDrive:     Float = 0.65f
        @JvmField var pfWet:       Float = 0.50f
        @JvmField var pfAlpha:     Float = 0.50f
        @JvmField var pfBeta:      Float = 0.50f
        @JvmField var pfDelta:     Float = 0.50f
        @JvmField var pfSigma:     Float = 0.50f
        @JvmField var pfFreq:      Float = 1000f
        @JvmField var pfResonance: Float = 0.707f
        @JvmField var pfMix:       Float = 0.70f
        @JvmField var pfLowGain:   Float = 0.0f
        @JvmField var pfMidGain:   Float = 0.0f
        @JvmField var pfHighGain:  Float = 0.0f
        @JvmField var pfPresence:  Float = 0.0f
        @JvmField var pfAmpModel:  Int   = 0

        /**
         * Slider [0..1] → ganancia dB [-18..+18]
         * Rango ampliado de ±12 a ±18 dB para mayor dinámica.
         */
        fun sliderToDb(slider: Float): Float = slider * 36f - 18f

        /**
         * Ganancia dB [-18..+18] → slider [0..1]
         */
        fun dbToSlider(db: Float): Float = (db + 18f) / 36f

        /**
         * Slider [0..1] → drive con curva logarítmica suave.
         * Permite control fino en valores bajos y agresivo en altos.
         * Rango efectivo: 0.0 (limpio) → 4.0 (saturación máxima).
         */
        fun sliderToDrive(slider: Float): Float =
            (Math.pow(slider.toDouble(), 2.0) * 4.0).toFloat().coerceIn(0f, 4f)

        /**
         * Drive [0..4] → slider [0..1]
         */
        fun driveToSlider(drive: Float): Float =
            Math.sqrt((drive / 4.0).coerceIn(0.0, 1.0)).toFloat()

        /** Slider [0..1] → frecuencia Hz [20..20000] (escala logarítmica) */
        fun sliderToFreq(slider: Float): Float =
            (20.0 * Math.pow(1000.0, slider.toDouble())).toFloat()

        /** Slider [0..1] → factor Q [0.1..10] (escala logarítmica) */
        fun sliderToQ(slider: Float): Float =
            (0.1 * Math.pow(100.0, slider.toDouble())).toFloat()
    }
}
