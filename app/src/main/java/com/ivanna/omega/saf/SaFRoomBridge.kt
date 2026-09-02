package com.ivanna.omega.saf

import com.ivanna.omega.core.IvannaNativeLib

/**
 * SaFRoomBridge — JNI Kotlin-side interface para SaFRoomOptimizer (C++).
 *
 * Implementa la fórmula:
 *   Φ_SAF-Room^∞ = lim Proj_S^{M_t}( p_t + α*(R_t,H_t,S_t) · M_t^{-1} · Δ_t )
 *   M_t = G_t + λ_t · I  ≻  0
 *
 * Flujo de uso:
 *   1. setTarget(floatArrayOf(...))     — fijar τ_t desde calibración HRTF
 *   2. setRoomState(rt60, drr, mode)    — actualizar R_t (RoomSimulator)
 *   3. setHrtfState(mismatch, conv)     — actualizar H_t (SaFOptimizer)
 *   4. setSoundFieldState(diff, comp)   — actualizar S_t (SpatialEngine)
 *   5. step()                           — ejecutar un paso Φ_SAF-Room^∞
 *   6. getDiagnostics()                 — leer [α*, E, λ, σ, iter]
 */
object SaFRoomBridge {

    private val loaded get() = IvannaNativeLib.isLoaded

    // ── Fórmula principal ────────────────────────────────────────────────────

    /**
     * Ejecuta un paso Riemanniano de Φ_SAF-Room^∞.
     * @return α*(R_t, H_t, S_t) — paso óptimo usado en este ciclo, ó 0f si falla.
     */
    fun step(): Float =
        if (loaded) runCatching { nativeSafrStep() }.getOrElse { 0f } else 0f

    // ── Contexto de sala R_t ─────────────────────────────────────────────────
    /**
     * @param rt60     Tiempo de reverberación T60 [0, 5] s
     * @param drr      Direct-to-reverb ratio [dB], típico [-10, +15]
     * @param roomMode Energía de modos de sala (0=sin modos, 1=dominante)
     */
    fun setRoomState(rt60: Float, drr: Float, roomMode: Float = 0f) {
        if (loaded) runCatching { nativeSafrSetRoom(rt60, drr, roomMode) }
    }

    // ── Estado HRTF H_t ───────────────────────────────────────────────────────
    /**
     * @param mismatchEnergy  ‖q_t − τ_measured‖² normalizado [0, 1]
     * @param convergenceRate EMA de ‖Δ_t‖ / ‖Δ_0‖ (0=convergido, 1=inicio)
     */
    fun setHrtfState(mismatchEnergy: Float, convergenceRate: Float = 0f) {
        if (loaded) runCatching { nativeSafrSetHrtf(mismatchEnergy, convergenceRate) }
    }

    // ── Campo sonoro S_t ──────────────────────────────────────────────────────
    /**
     * @param diffuseness Difusividad del campo (0=onda plana, 1=difuso)
     * @param complexity  Complejidad espectral normalizada [0, 1]
     */
    fun setSoundFieldState(diffuseness: Float, complexity: Float = 0f) {
        if (loaded) runCatching { nativeSafrSetField(diffuseness, complexity) }
    }

    // ── Target τ_t ────────────────────────────────────────────────────────────
    /**
     * Fija el target de calibración perceptual (vector latente de 7D).
     * Se obtiene de SaFOptimizer (calibración HRTF por direcciones).
     */
    fun setTarget(tau: FloatArray) {
        if (loaded && tau.size >= 7)
            runCatching { nativeSafrSetTarget(tau) }
    }

    // ── Lectura de estado ─────────────────────────────────────────────────────
    /** p_t actual — vector latente HRTF/DSP (7D). */
    fun getParams(): FloatArray =
        if (loaded) runCatching { nativeSafrGetParams() }.getOrElse { FloatArray(7) }
        else FloatArray(7)

    /**
     * Diagnósticos del último paso:
     * [0] α*         — paso óptimo
     * [1] E_t        — error Mahalanobis ‖Δ‖²_{M_t}
     * [2] λ_t        — regularización adaptativa
     * [3] σ(R,H,S)   — acoplamiento sala/HRTF/campo
     * [4] iteration  — contador de pasos
     */
    fun getDiagnostics(): FloatArray =
        if (loaded) runCatching { nativeSafrDiag() }.getOrElse { FloatArray(5) }
        else FloatArray(5)

    fun reset() { if (loaded) runCatching { nativeSafrReset() } }

    // ── JNI ──────────────────────────────────────────────────────────────────
    @JvmStatic private external fun nativeSafrStep(): Float
    @JvmStatic private external fun nativeSafrSetRoom(rt60: Float, drr: Float, roomMode: Float)
    @JvmStatic private external fun nativeSafrSetHrtf(mismatch: Float, convRate: Float)
    @JvmStatic private external fun nativeSafrSetField(diffuseness: Float, complexity: Float)
    @JvmStatic private external fun nativeSafrSetTarget(tau: FloatArray)
    @JvmStatic private external fun nativeSafrGetParams(): FloatArray
    @JvmStatic private external fun nativeSafrDiag(): FloatArray
    @JvmStatic private external fun nativeSafrReset()
}
