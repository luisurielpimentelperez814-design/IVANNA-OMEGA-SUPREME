// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// ============================================================
// IVANNA OMEGA SUPREME — Unified Audio Control Plane (impl)
//
// Este archivo NO toca los engines DSP directamente. La arquitectura
// del proyecto (ver control_frame.hpp) exige que TODO cambio de
// parámetro fluya a través del ControlFrameBus (seqlock SPSC) para
// mantener el determinismo por bloque en el hilo de audio.
//
// Rol de este archivo:
//   1. Mantener el singleton UnifiedControlFrame (g_control_frame),
//      donde Kotlin/YAMNet/PhaseOracle/EvoKernel publican sus scores
//      y predicciones de forma atómica y desde múltiples hilos.
//   2. Cada vez que control_apply_frame() es llamado (desde el hilo
//      de control JNI, NO desde el hilo de audio), fusiona el estado
//      actual del UnifiedControlFrame con el último g_staging_frame
//      del bus y publica un ControlFrame nuevo, que el hilo de audio
//      consumirá al principio del siguiente bloque via
//      apply_pending_control_frame().
//
// Con esto, el "orquestador central" queda plenamente integrado en el
// pipeline unificado sin violar la propiedad de determinismo por bloque
// ni la encapsulación de PDEngine/DSP.
// ============================================================

#include "audio_control_plane.hpp"
#include "control_frame.hpp"
#include <algorithm>
#include <cstdio>
#include <android/log.h>

#define ALOG(level, tag, fmt, ...) __android_log_print(level, tag, fmt, ##__VA_ARGS__)

// ── Global singleton (declarado en audio_control_plane.hpp) ─────
UnifiedControlFrame g_control_frame;

// ── Bus + staging frame propiedad de ivanna_omega_jni.cpp ───────
// Se exponen no-static para poder publicar desde aquí también.
namespace ivanna {
    extern ControlFrameBus g_control_bus;
    extern ControlFrame    g_staging_frame;
}

// ── Implementation: control_apply_frame() ───────────────────────
//
// Fusiona el UnifiedControlFrame (YAMNet + PhaseOracle + AudioEngine +
// Evo genome) con el último ControlFrame publicado, y publica el
// resultado en el bus seqlock. Debe llamarse desde el hilo de control
// (JNI/UI) — NUNCA desde el audio thread — con la misma disciplina que
// los setters JNI del proyecto (ver ivanna_omega_jni.cpp).
//
// Devuelve el número de campos ajustados por la fusión (para debug/telemetry).

int control_apply_frame() noexcept {
    using namespace ivanna;

    int updates = 0;
    constexpr const char* TAG = "ControlPlane";

    // ────────────────────────────────────────────────────────────────
    // 1. Snapshot del UnifiedControlFrame (fuente cross-thread)
    // ────────────────────────────────────────────────────────────────
    const auto yamnet_voice   = g_control_frame.yamnet_voice_score.load(std::memory_order_relaxed);
    const auto yamnet_bass    = g_control_frame.yamnet_bass_score.load(std::memory_order_relaxed);

    const auto eq_gain_db     = g_control_frame.eq_gain_db.load(std::memory_order_relaxed);
    const auto exciter_wet    = g_control_frame.exciter_wet.load(std::memory_order_relaxed);
    const auto widener_stereo = g_control_frame.widener_stereo.load(std::memory_order_relaxed);

    const auto nho_harmonic   = g_control_frame.nho_harmonic_gain.load(std::memory_order_relaxed);
    const auto spatial_angle  = g_control_frame.spatial_angle_deg.load(std::memory_order_relaxed);
    const auto spatial_width  = g_control_frame.spatial_width.load(std::memory_order_relaxed);
    const auto pd_mode        = g_control_frame.pd_mode.load(std::memory_order_relaxed);

    const auto audio_engine_exciter = g_control_frame.audio_engine_exciter.load(std::memory_order_relaxed);
    const auto audio_engine_eq      = g_control_frame.audio_engine_eq_gain.load(std::memory_order_relaxed);
    const auto audio_engine_width   = g_control_frame.audio_engine_width.load(std::memory_order_relaxed);

    const auto evo_active = g_control_frame.evolutionary_active.load(std::memory_order_relaxed);

    // ────────────────────────────────────────────────────────────────
    // 2. YAMNet → ajustes dinámicos del pipeline
    // ────────────────────────────────────────────────────────────────
    float yamnet_widener_mult = 1.f;
    float yamnet_eq_boost_2k  = 0.f;
    if (yamnet_voice > 0.6f) {
        yamnet_eq_boost_2k = (yamnet_voice - 0.6f) * 3.f;   // +0..1.2 dB
        updates++;
    }
    if (yamnet_bass > 0.7f) {
        yamnet_widener_mult = 0.7f;                          // reduce width si hay bajos
        updates++;
    }

    // ────────────────────────────────────────────────────────────────
    // 3. Construye un ControlFrame nuevo a partir del staging vigente
    //    (respeta la disciplina "snapshot inmutable" del bus seqlock)
    // ────────────────────────────────────────────────────────────────
    ControlFrame f = g_staging_frame;

    // EQ: gain combinado (control + YAMNet + AudioEngine) mapeado a 'mid'
    const float combined_eq_gain = eq_gain_db + yamnet_eq_boost_2k + audio_engine_eq;
    f.mid = std::clamp(combined_eq_gain, -18.f, 18.f);
    updates++;

    // Exciter: fusiona AudioEngine + control frame → 'wet'
    const float combined_exciter = std::min(1.f, exciter_wet + audio_engine_exciter);
    f.wet = std::clamp(combined_exciter, 0.f, 1.f);
    updates++;

    // Widener: fusiona AudioEngine + YAMNet
    const float combined_width = (widener_stereo + audio_engine_width) * 0.5f * yamnet_widener_mult;
    // El ancho estéreo del pipeline se controla vía nho_wet (0..1) para el bloque spatial
    f.nho_wet = std::clamp(combined_width, 0.f, 1.f);
    updates++;

    // PDEngine: modo + spatial (respeta rangos válidos del enum interno)
    f.mode              = std::clamp(pd_mode, 0, 2);
    f.spatial_angle_deg = std::clamp(spatial_angle, 0.f, 120.f);
    f.spatial_width     = std::clamp(spatial_width, 0.f, 1.5f);
    updates += 3;

    // NHO harmonic gain
    f.nho_harmonic_gain = std::clamp(nho_harmonic, 0.f, 2.f);
    updates++;

    // ────────────────────────────────────────────────────────────────
    // 4. Evolutionary genome mapping (real-time, si está activo)
    // ────────────────────────────────────────────────────────────────
    if (evo_active) {
        const float evo_drive     = g_control_frame.evo_genome_dsp[0].load(std::memory_order_relaxed);
        const float evo_resonance = g_control_frame.evo_genome_dsp[1].load(std::memory_order_relaxed);

        float evo_nho[4];
        for (int i = 0; i < 4; ++i) {
            evo_nho[i] = g_control_frame.evo_genome_nho[i].load(std::memory_order_relaxed);
        }
        float evo_spatial[3];
        for (int i = 0; i < 3; ++i) {
            evo_spatial[i] = g_control_frame.evo_genome_spatial[i].load(std::memory_order_relaxed);
        }

        // DSP: drive/resonance
        f.drive     = std::clamp(evo_drive,     0.f, 1.f);
        f.resonance = std::clamp(0.3f + evo_resonance * 1.7f, 0.3f, 2.0f);

        // NHO: alpha 0.5..0.9, beta 0.1..0.4, wet 0.05..0.35, harmonic 0..1
        f.nho_alpha         = 0.5f  + evo_nho[0] * 0.4f;
        f.nho_beta          = 0.1f  + evo_nho[1] * 0.3f;
        f.nho_wet           = std::clamp(0.05f + evo_nho[2] * 0.3f, 0.f, 1.f);
        f.nho_harmonic_gain = std::clamp(evo_nho[3], 0.f, 2.f);

        // Spatial
        f.spatial_angle_deg = std::clamp(evo_spatial[0] * 120.f, 0.f, 120.f);
        f.spatial_width     = std::clamp(evo_spatial[1] * 1.5f,  0.f, 1.5f);

        updates += 8;

        ALOG(ANDROID_LOG_VERBOSE, TAG,
             "evo→frame: drive=%.2f res=%.2f nho[a=%.2f b=%.2f w=%.2f h=%.2f] sp[a=%.1f w=%.2f]",
             f.drive, f.resonance, f.nho_alpha, f.nho_beta, f.nho_wet, f.nho_harmonic_gain,
             f.spatial_angle_deg, f.spatial_width);
    }

    // ────────────────────────────────────────────────────────────────
    // 5. Publish snapshot en el bus seqlock
    //    (el audio thread lo consumirá en el siguiente bloque)
    // ────────────────────────────────────────────────────────────────
    g_staging_frame = f;             // mantén el staging alineado con lo publicado
    g_control_bus.publish(f);

    return updates;
}
