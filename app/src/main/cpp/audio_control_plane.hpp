// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Unified Audio Control Plane
 *
 * Orquestador central que sincroniza:
 *   1. YAMNet classification scores
 *   2. DSP pipeline (EQ, Compresor, Exciter, Widener)
 *   3. PDEngine (NHO + Spatial + Evolutionary)
 *   4. Phase Oracle predictions (para BiquadBank)
 *   5. AudioEngine parámetros fusionados
 *   6. Evolutionary adapter mapeo real-time
 *
 * TODO corre en audio thread (FIFO RT-safe, lock-free).
 * Parámetros atómicos evitan bloqueos.
 * ============================================================
 */

// ── Rango de parámetros normalizados (0..1 o ±1)
constexpr float CONTROL_MIN = 0.f;
constexpr float CONTROL_MAX = 1.f;
constexpr float CONTROL_NEUTRAL = 0.5f;

// ── Estructura de control unificada ─────────────────────────────
struct UnifiedControlFrame {
    // ── YAMNet Classification Scores (cada ~1s desde Kotlin) ────────
    std::atomic<float> yamnet_voice_score{0.f};      // [0..1] confianza de voz
    std::atomic<float> yamnet_music_score{0.f};      // [0..1] confianza de música
    std::atomic<float> yamnet_bass_score{0.f};       // [0..1] confianza de bajos
    std::atomic<float> yamnet_silence_score{0.f};    // [0..1] confianza de silencio

    // ── DSP Pipeline State (sincronizado desde DSPBridge) ──────────
    std::atomic<float> eq_gain_db{0.f};              // ±18 dB
    std::atomic<float> comp_threshold_db{-12.f};     // -24..0 dB
    std::atomic<float> comp_ratio{4.f};              // 1:1..20:1
    std::atomic<float> exciter_wet{0.f};             // [0..1] mezcla exciter
    std::atomic<float> widener_stereo{0.f};          // [0..1] ancho estéreo

    // ── PDEngine Mode & Parameters ──────────────────────────────────
    std::atomic<int> pd_mode{0};                     // 0=DSP, 1=+NHO, 2=+NHO+Spatial
    std::atomic<float> nho_harmonic_gain{0.2f};      // [0..1] intensidad armónica
    std::atomic<float> spatial_angle_deg{0.f};       // [0..120] ITD/ILD ángulo
    std::atomic<float> spatial_width{0.5f};          // [0..1.5] ancho del campo
    std::atomic<int> evo_mode{0};                    // 0=off, 1=on (kernel corre)

    // ── Phase Oracle Prediction ────────────────────────────────────
    std::atomic<float> phase_oracle_T_refined{0.f};  // periodo refinado (muestras)
    std::atomic<float> phase_coherence{0.f};         // [0..1] coherencia estimada

    // ── AudioEngine Fusión ─────────────────────────────────────────
    std::atomic<float> audio_engine_exciter{0.f};    // [0..1] excitation from AudioEngine
    std::atomic<float> audio_engine_eq_gain{0.f};    // dB from AudioEngine
    std::atomic<float> audio_engine_width{0.f};      // [0..1] stereo width from AudioEngine

    // ── Evolutionary Best Genome (actualizado cada 50ms) ───────────
    std::atomic<float> evo_genome_dsp[5]{};          // [0..4] DSP params
    std::atomic<float> evo_genome_nho[4]{};          // [5..8] NHO params
    std::atomic<float> evo_genome_spatial[3]{};      // [9..11] Spatial params

    // ── Telemetry & Monitoring ────────────────────────────────────
    std::atomic<float> output_lufs{-23.f};           // LUFS actual
    std::atomic<float> output_peak_dbfs{-6.f};       // pico actual

    // ── Control Flow Flags ─────────────────────────────────────────
    std::atomic<bool> anti_dolby_enabled{false};
    std::atomic<bool> spatial_rendering_active{false};
    std::atomic<bool> evolutionary_active{false};

    // ── Resets & Initializers ──────────────────────────────────────
    void reset() noexcept {
        yamnet_voice_score.store(0.f, std::memory_order_relaxed);
        yamnet_music_score.store(0.f, std::memory_order_relaxed);
        yamnet_bass_score.store(0.f, std::memory_order_relaxed);
        yamnet_silence_score.store(0.f, std::memory_order_relaxed);
        eq_gain_db.store(0.f, std::memory_order_relaxed);
        comp_threshold_db.store(-12.f, std::memory_order_relaxed);
        comp_ratio.store(4.f, std::memory_order_relaxed);
        exciter_wet.store(0.f, std::memory_order_relaxed);
        widener_stereo.store(0.f, std::memory_order_relaxed);
        pd_mode.store(0, std::memory_order_relaxed);
        nho_harmonic_gain.store(0.2f, std::memory_order_relaxed);
        spatial_angle_deg.store(0.f, std::memory_order_relaxed);
        spatial_width.store(0.5f, std::memory_order_relaxed);
        evo_mode.store(0, std::memory_order_relaxed);
        phase_oracle_T_refined.store(0.f, std::memory_order_relaxed);
        phase_coherence.store(0.f, std::memory_order_relaxed);
        audio_engine_exciter.store(0.f, std::memory_order_relaxed);
        audio_engine_eq_gain.store(0.f, std::memory_order_relaxed);
        audio_engine_width.store(0.f, std::memory_order_relaxed);
        anti_dolby_enabled.store(false, std::memory_order_relaxed);
        spatial_rendering_active.store(false, std::memory_order_relaxed);
        evolutionary_active.store(false, std::memory_order_relaxed);
    }
};

// ── Global singleton control frame ───────────────────────────────
extern UnifiedControlFrame g_control_frame;

// ============================================================
// Unified Control Plane — Funciones Públicas (JNI-accessible)
// ============================================================

/*
 * Inyecta scores de YAMNet clasificador desde Kotlin.
 * Llamado cada ~1s desde AudioPipeline.kt via AudioEngine.nativeSetAntiDolbyScores()
 */
inline void control_set_yamnet_scores(float voice, float music, float bass, float silence) noexcept {
    // Normalizar a [0..1] si es necesario
    voice = std::clamp(voice, 0.f, 1.f);
    music = std::clamp(music, 0.f, 1.f);
    bass = std::clamp(bass, 0.f, 1.f);
    silence = std::clamp(silence, 0.f, 1.f);

    g_control_frame.yamnet_voice_score.store(voice, std::memory_order_relaxed);
    g_control_frame.yamnet_music_score.store(music, std::memory_order_relaxed);
    g_control_frame.yamnet_bass_score.store(bass, std::memory_order_relaxed);
    g_control_frame.yamnet_silence_score.store(silence, std::memory_order_relaxed);
}

/*
 * Inyecta predicción de Phase Oracle en el orquestador.
 * Llamado desde phase_oracle_engine.hpp::predict()
 */
inline void control_set_phase_oracle(float T_refined, float coherence) noexcept {
    T_refined = std::clamp(T_refined, 0.f, 10000.f);  // muestras (48kHz = ~208ms max)
    coherence = std::clamp(coherence, 0.f, 1.f);

    g_control_frame.phase_oracle_T_refined.store(T_refined, std::memory_order_relaxed);
    g_control_frame.phase_coherence.store(coherence, std::memory_order_relaxed);
}

/*
 * Inyecta mejor genoma del kernel evolutivo (llamado cada ~50ms).
 * genome: array de 256 floats, [0..4]=DSP, [5..8]=NHO, [9..11]=Spatial
 */
inline void control_set_evo_genome(const float* genome, int genome_size) noexcept {
    if (!genome || genome_size < 12) return;

    // DSP params [0..4]
    for (int i = 0; i < 5 && i < genome_size; ++i) {
        g_control_frame.evo_genome_dsp[i].store(std::clamp(genome[i], 0.f, 1.f), std::memory_order_relaxed);
    }

    // NHO params [5..8]
    for (int i = 0; i < 4 && (5 + i) < genome_size; ++i) {
        g_control_frame.evo_genome_nho[i].store(std::clamp(genome[5 + i], 0.f, 1.f), std::memory_order_relaxed);
    }

    // Spatial params [9..11]
    for (int i = 0; i < 3 && (9 + i) < genome_size; ++i) {
        g_control_frame.evo_genome_spatial[i].store(std::clamp(genome[9 + i], 0.f, 1.f), std::memory_order_relaxed);
    }
}

/*
 * Aplica control frame al motor DSP/PDEngine (punto de sincronización).
 * Llamado desde el audio thread en cada bloque de procesamiento.
 *
 * Retorna: cantidad de parámetros actualizados (debug).
 */
int control_apply_frame() noexcept;

// ── Getters para debug/telemetry ───────────────────────────────
inline float control_get_yamnet_voice_score() noexcept {
    return g_control_frame.yamnet_voice_score.load(std::memory_order_relaxed);
}

inline int control_get_pd_mode() noexcept {
    return g_control_frame.pd_mode.load(std::memory_order_relaxed);
}

inline float control_get_phase_oracle_T() noexcept {
    return g_control_frame.phase_oracle_T_refined.load(std::memory_order_relaxed);
}

// NOTE: este header usa #pragma once (línea 2). No lleva #endif final.
