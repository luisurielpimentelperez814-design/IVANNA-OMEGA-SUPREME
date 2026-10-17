package com.ivanna.omega.core

/**
 * Ivanna4DNative — puente Kotlin de la Cadena 4D (excitación anticipada, háptica
 * multibanda, dosímetro WHO/H.870, cancelación de diafonía, fusión 6DoF).
 *
 * La lógica real vive en C++ (`music_intelligence/FourDControlSurface`) y la ruta de
 * audio la alimenta desde `ImeBridge`. Este objeto es la cara pública para la UI y la
 * telemetría: control + estado. Todo APAGADO por defecto en el lado nativo.
 *
 * Carga perezosa y única vía [NativeLibraryLoader] (punto único del proyecto).
 */
object Ivanna4DNative {

    @Volatile private var available: Boolean = false

    init {
        available = NativeLibraryLoader.ensureLoaded()
    }

    /** true si libivanna_omega.so se cargó (los `external fun` son invocables). */
    val isAvailable: Boolean get() = available

    // Ciclo de vida: la cadena se prepara en el arranque nativo del DSP (nativeInit /
    // nativeInitDSP) con el sample rate real; ya no hay "ensure" desde la UI.

    // ── Interruptor maestro ──────────────────────────────────────────────────
    @JvmStatic external fun nativeFourDSetEnabled(on: Boolean)

    // ── Sub-controles ────────────────────────────────────────────────────────
    @JvmStatic external fun nativeFourDSetHaptics(on: Boolean)
    @JvmStatic external fun nativeFourDSetHapticScale(s: Float)
    @JvmStatic external fun nativeFourDSetOutputLatencyMs(ms: Float)
    @JvmStatic external fun nativeFourDSetLeadMs(ms: Float)
    @JvmStatic external fun nativeFourDSetDepth(d: Float)
    @JvmStatic external fun nativeFourDSetArc(on: Boolean)
    @JvmStatic external fun nativeFourDSetArcDepth(d: Float)
    @JvmStatic external fun nativeFourDSetDosimeterMode(mode: Int)
    @JvmStatic external fun nativeFourDSetDosimeterCalibration(dbfsToSpl: Float)
    @JvmStatic external fun nativeFourDSetCrosstalk(on: Boolean)

    // ── Telemetría ───────────────────────────────────────────────────────────
    @JvmStatic external fun nativeFourDStatusJson(): String
}
