// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

#include <cmath>
#include <algorithm>
#include <atomic>

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Evolutionary Adapter v2 (Enhanced)
 *
 * Mapea el mejor genoma del kernel evolutivo a parámetros reales
 * en tiempo real (cada ~50ms, sin bloquear audio thread).
 *
 * Genoma structure (256 floats):
 *   [0..4]   → DSP params (drive, wet, freq, Q, saturation)
 *   [5..8]   → NHO params (alpha, beta, mu, harmonic_gain)
 *   [9..11]  → Spatial params (angle, width, wet)
 *   [12..15] → BiquadBank (attack, release, freq, resonance)
 *   [16..255]→ reservado para expansión futura
 *
 * Fitness perceptual:
 *   F = L * (1 - 0.8*|T_delta|) * (1 + 0.3*S)
 *   L = loudness (LUFS), T_delta = transient coherence delta,
 *   S = spatial coherence
 * ============================================================
 */

struct EvolutionaryGenomeMapping {
    // ── Genoma actual (actualizado cada ~50ms desde kernel) ────────────
    static constexpr int GENOME_SIZE = 256;
    std::atomic<float> genome[GENOME_SIZE];

    // ── Fitness tracking ───────────────────────────────────────────────
    std::atomic<float> best_fitness{-1e6f};
    std::atomic<int> generation{0};

    // ── DSP mapped parameters ──────────────────────────────────────────
    float dsp_drive = 1.f;              // [0..1] → 1.0..1.5x pre-gain
    float dsp_wet = 0.5f;               // [0..1] → mezcla DSP
    float dsp_freq = 2000.f;            // [0..1] → 100..8000 Hz
    float dsp_resonance = 1.f;          // [0..1] → 0.5..5.0 Q
    float dsp_saturation = 0.f;         // [0..1] → 0..1 intensidad

    // ── NHO mapped parameters ──────────────────────────────────────────
    float nho_alpha = 0.8f;             // [0..1] → 0.5..0.9 input coupling
    float nho_beta = 0.3f;              // [0..1] → 0.1..0.4 state feedback
    float nho_mu = 0.15f;               // [0..1] → 0.05..0.35 step size
    float nho_harmonic = 0.2f;          // [0..1] → 0..1 harmonic intensity

    // ── Spatial mapped parameters ──────────────────────────────────────
    float spatial_angle = 0.f;          // [0..1] → 0..90 grados
    float spatial_width = 0.5f;         // [0..1] → 0..1 ancho
    float spatial_wet = 0.3f;           // [0..1] → 0..1 mezcla

    // ── BiquadBank envelope parameters ─────────────────────────────────
    float biquad_attack = 10.f;         // [0..1] → 1..100 ms
    float biquad_release = 50.f;        // [0..1] → 10..500 ms
    float biquad_freq = 1000.f;         // [0..1] → 100..8000 Hz
    float biquad_resonance = 1.f;       // [0..1] → 0.5..5.0 Q

    // ── Inicialización ─────────────────────────────────────────────────
    void init() noexcept {
        for (int i = 0; i < GENOME_SIZE; ++i) {
            genome[i].store(0.5f, std::memory_order_relaxed);  // valor neutral
        }
        best_fitness.store(-1e6f, std::memory_order_relaxed);
        generation.store(0, std::memory_order_relaxed);
    }

    // ── Inyectar mejor genoma desde kernel evolutivo ────────────────────
    // (llamado desde evolutionary_kernel.cpp cada ~50ms)
    void update_best_genome(const float* best_gen, float fitness) noexcept {
        if (!best_gen) return;

        // Actualizar genoma atómicamente
        for (int i = 0; i < GENOME_SIZE && i < 256; ++i) {
            genome[i].store(std::clamp(best_gen[i], 0.f, 1.f), std::memory_order_relaxed);
        }

        best_fitness.store(fitness, std::memory_order_relaxed);
        int gen = generation.load(std::memory_order_relaxed);
        generation.store(gen + 1, std::memory_order_relaxed);
    }

    // ── Mapeo de genoma → parámetros reales ───────────────────────────
    // Llamado desde audio thread (via audio_control_plane.cpp)
    void apply_mapping() noexcept {
        // Leer genoma actual (lock-free)
        float g[GENOME_SIZE];
        for (int i = 0; i < GENOME_SIZE; ++i) {
            g[i] = genome[i].load(std::memory_order_relaxed);
        }

        // ── DSP mapping [0..4] ────────────────────────────────────────
        // genome[0] → drive (pre-gain)
        dsp_drive = 1.f + g[0] * 0.5f;  // 1.0..1.5x

        // genome[1] → wet (DSP mezcla)
        dsp_wet = 0.3f + g[1] * 0.4f;   // 0.3..0.7

        // genome[2] → frequency center (100..8000 Hz)
        dsp_freq = 100.f + g[2] * 7900.f;

        // genome[3] → resonance (Q: 0.5..5.0)
        dsp_resonance = 0.5f + g[3] * 4.5f;

        // genome[4] → saturation intensity
        dsp_saturation = g[4];  // 0..1

        // ── NHO mapping [5..8] ────────────────────────────────────────
        // genome[5] → alpha (input coupling)
        nho_alpha = 0.5f + g[5] * 0.4f;  // 0.5..0.9

        // genome[6] → beta (state feedback)
        nho_beta = 0.1f + g[6] * 0.3f;   // 0.1..0.4

        // genome[7] → mu (step size / gating)
        nho_mu = 0.05f + g[7] * 0.3f;    // 0.05..0.35

        // genome[8] → harmonic intensity
        nho_harmonic = g[8];  // 0..1

        // ── Spatial mapping [9..11] ──────────────────────────────────
        // genome[9] → angle (0..90 grados)
        spatial_angle = g[9] * 90.f;

        // genome[10] → width (0..1)
        spatial_width = g[10];

        // genome[11] → wet mix
        spatial_wet = g[11] * 0.5f;  // 0..0.5

        // ── BiquadBank envelope mapping [12..15] ─────────────────────
        // genome[12] → attack (1..100 ms)
        biquad_attack = 1.f + g[12] * 99.f;

        // genome[13] → release (10..500 ms)
        biquad_release = 10.f + g[13] * 490.f;

        // genome[14] → frequency
        biquad_freq = 100.f + g[14] * 7900.f;

        // genome[15] → resonance
        biquad_resonance = 0.5f + g[15] * 4.5f;
    }

    // ── Getters para acceso desde audio_control_plane ──────────────────
    inline float get_dsp_drive() const noexcept { return dsp_drive; }
    inline float get_dsp_wet() const noexcept { return dsp_wet; }
    inline float get_dsp_freq() const noexcept { return dsp_freq; }
    inline float get_dsp_resonance() const noexcept { return dsp_resonance; }
    inline float get_dsp_saturation() const noexcept { return dsp_saturation; }

    inline float get_nho_alpha() const noexcept { return nho_alpha; }
    inline float get_nho_beta() const noexcept { return nho_beta; }
    inline float get_nho_mu() const noexcept { return nho_mu; }
    inline float get_nho_harmonic() const noexcept { return nho_harmonic; }

    inline float get_spatial_angle() const noexcept { return spatial_angle; }
    inline float get_spatial_width() const noexcept { return spatial_width; }
    inline float get_spatial_wet() const noexcept { return spatial_wet; }

    inline float get_biquad_attack() const noexcept { return biquad_attack; }
    inline float get_biquad_release() const noexcept { return biquad_release; }
    inline float get_biquad_freq() const noexcept { return biquad_freq; }
    inline float get_biquad_resonance() const noexcept { return biquad_resonance; }

    inline float get_best_fitness() const noexcept {
        return best_fitness.load(std::memory_order_relaxed);
    }

    inline int get_generation() const noexcept {
        return generation.load(std::memory_order_relaxed);
    }
};

// ── Global singleton ──────────────────────────────────────────
extern EvolutionaryGenomeMapping g_evo_adapter;

#endif  // EVOLUTIONARY_ADAPTER_ENHANCED_HPP
