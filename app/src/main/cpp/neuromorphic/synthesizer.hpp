/**
 * synthesizer.hpp
 * IVANNA N-P-E — Synthesizer (C++17, AArch64/Clang)
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Contraparte de AutonomousBrain: recibe los 5 parámetros de perfil tonal
 * (bass_weight, mid_presence, treble_air, warmth, clarity) desde el hilo de
 * análisis y los suaviza con un One-Pole smoother para que el hilo de audio
 * los consuma sin zipper noise.
 *
 * Contrato (documentado ya en autonomous_brain.hpp antes de que esta clase
 * existiera):
 *   • setTargetParameters() hace clamp a [-1,1] y escribe en atomics
 *     individuales (memory_order_release) — lock-free, callable desde
 *     cualquier hilo (el hilo de análisis de AutonomousBrain).
 *   • processFrameSmoothing(numFrames) se llama desde el hilo de audio antes
 *     de generar cada bloque; avanza el smoother numFrames muestras y deja
 *     los valores suavizados listos en current().
 *
 * Restricciones heredadas del proyecto:
 *   • Zero heap allocation — sin new/malloc/std::vector
 *   • Sin locks — solo atomics con orden de memoria explícito
 *   • -O3 -ffast-math -fno-exceptions -fno-rtti -std=c++17
 */
#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>

namespace ivanna::acoustic {

/// Snapshot de los 5 parámetros de perfil tonal, todos en [-1, 1].
struct SynthParams {
    float bass_weight  = 0.f;
    float mid_presence = 0.f;
    float treble_air   = 0.f;
    float warmth       = 0.f;
    float clarity      = 0.f;
};

class Synthesizer {
public:
    /**
     * @param sampleRate   Frecuencia de muestreo en Hz (típicamente 48000).
     * @param smoothingMs  Constante de tiempo del One-Pole en milisegundos.
     *                     50ms es un valor conservador: suficientemente
     *                     rápido para seguir cambios de género/dinámica de
     *                     AutonomousBrain (~85ms por ventana) sin producir
     *                     saltos audibles.
     */
    explicit Synthesizer(float sampleRate = 48000.f, float smoothingMs = 50.f) noexcept
        : sample_rate_(sampleRate)
    {
        setSmoothingTimeMs(smoothingMs);
    }

    /// Reconfigura la velocidad del smoother sin tocar los valores actuales.
    inline void setSmoothingTimeMs(float ms) noexcept {
        const float tau_samples = (ms * 0.001f) * sample_rate_;
        const float safe_tau    = (tau_samples < 1.0f) ? 1.0f : tau_samples;
        per_sample_coeff_ = __builtin_expf(-1.0f / safe_tau);
    }

    // ── API llamada desde el hilo de análisis (AutonomousBrain) ────────────

    /**
     * setTargetParameters — punto de entrada documentado y usado por
     * AutonomousBrain::evaluateAndDrive(). Clampa a [-1,1] y publica de
     * forma lock-free con release semantics.
     */
    inline void setTargetParameters(float bass, float mid, float treble,
                                     float warmth, float clarity) noexcept {
        target_bass_.store(clamp11(bass),       std::memory_order_relaxed);
        target_mid_.store(clamp11(mid),         std::memory_order_relaxed);
        target_treble_.store(clamp11(treble),   std::memory_order_relaxed);
        target_warmth_.store(clamp11(warmth),   std::memory_order_relaxed);
        // El último store lleva el release: garantiza que los 4 anteriores
        // sean visibles para el hilo de audio antes de ver este cambio.
        target_clarity_.store(clamp11(clarity), std::memory_order_release);
        has_target_.store(true, std::memory_order_release);
    }

    /// Reinicia smoother y targets a silencio neutro (todo en 0).
    inline void reset() noexcept {
        target_bass_.store(0.f, std::memory_order_relaxed);
        target_mid_.store(0.f, std::memory_order_relaxed);
        target_treble_.store(0.f, std::memory_order_relaxed);
        target_warmth_.store(0.f, std::memory_order_relaxed);
        target_clarity_.store(0.f, std::memory_order_release);
        current_ = SynthParams{};
        has_target_.store(false, std::memory_order_release);
    }

    // ── API llamada desde el hilo de audio ──────────────────────────────────

    /**
     * processFrameSmoothing — avanza el One-Pole smoother `numFrames`
     * muestras hacia el target más reciente. Debe llamarse una vez por
     * bloque de audio, antes de leer current().
     *
     * Complejidad O(1): un powf() por banda por bloque (5 llamadas),
     * no por muestra — barato incluso en bloques de 64-256 frames.
     */
    inline void processFrameSmoothing(int numFrames) noexcept {
        if (numFrames <= 0) return;
        if (!has_target_.load(std::memory_order_acquire)) return;

        // acquire en el último campo escrito por setTargetParameters()
        // garantiza ver los 5 valores consistentes (misma "transacción").
        const float t_clarity = target_clarity_.load(std::memory_order_acquire);
        const float t_bass    = target_bass_.load(std::memory_order_relaxed);
        const float t_mid     = target_mid_.load(std::memory_order_relaxed);
        const float t_treble  = target_treble_.load(std::memory_order_relaxed);
        const float t_warmth  = target_warmth_.load(std::memory_order_relaxed);

        // coeff^numFrames — equivalente a aplicar el one-pole muestra a
        // muestra sin el costo de iterar numFrames veces.
        const float block_coeff = __builtin_powf(per_sample_coeff_,
                                                   static_cast<float>(numFrames));
        const float one_minus   = 1.0f - block_coeff;

        current_.bass_weight  = current_.bass_weight  * block_coeff + t_bass    * one_minus;
        current_.mid_presence = current_.mid_presence * block_coeff + t_mid     * one_minus;
        current_.treble_air   = current_.treble_air   * block_coeff + t_treble  * one_minus;
        current_.warmth       = current_.warmth       * block_coeff + t_warmth  * one_minus;
        current_.clarity      = current_.clarity      * block_coeff + t_clarity * one_minus;
    }

    /// Valores suavizados listos para dar forma al audio (solo hilo de audio).
    [[nodiscard]] inline const SynthParams& current() const noexcept { return current_; }

    /// Alias directo a cada banda, útil para código que no quiere el struct.
    [[nodiscard]] inline float bassWeight()  const noexcept { return current_.bass_weight; }
    [[nodiscard]] inline float midPresence() const noexcept { return current_.mid_presence; }
    [[nodiscard]] inline float trebleAir()   const noexcept { return current_.treble_air; }
    [[nodiscard]] inline float warmth()      const noexcept { return current_.warmth; }
    [[nodiscard]] inline float clarity()     const noexcept { return current_.clarity; }

private:
    static inline float clamp11(float v) noexcept {
        return (v < -1.0f) ? -1.0f : (v > 1.0f ? 1.0f : v);
    }

    float sample_rate_;
    float per_sample_coeff_ = 0.f;

    // ── Targets (escritos por el hilo de análisis, atomics individuales) ──
    std::atomic<float> target_bass_{0.f};
    std::atomic<float> target_mid_{0.f};
    std::atomic<float> target_treble_{0.f};
    std::atomic<float> target_warmth_{0.f};
    std::atomic<float> target_clarity_{0.f};
    std::atomic<bool>  has_target_{false};

    // ── Estado suavizado (solo tocado por el hilo de audio) ────────────────
    SynthParams current_{};
};

} // namespace ivanna::acoustic
