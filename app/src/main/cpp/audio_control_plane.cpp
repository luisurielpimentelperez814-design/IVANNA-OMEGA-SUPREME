// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.

#include "audio_control_plane.hpp"
#include "dsp/ParametricEQ.h"
#include "dsp/Compressor.h"
#include "dsp/HarmonicExciter.h"
#include "dsp/StereoWidener.h"
#include "pd_engine.hpp"
#include <cstdio>
#include <android/log.h>

#define ALOG(level, tag, fmt, ...) __android_log_print(level, tag, fmt, ##__VA_ARGS__)

// ── Global singleton ──────────────────────────────────────────
UnifiedControlFrame g_control_frame;

// ── Externs (definidos en otros .cpp) ─────────────────────────
extern ParametricEQ g_eq;
extern Compressor g_comp;
extern HarmonicExciter g_exciter;
extern StereoWidener g_widener;
extern PDEngine g_pd;

// ── Implementation: control_apply_frame() ─────────────────────

int control_apply_frame() noexcept {
    int updates = 0;
    constexpr const char* TAG = "ControlPlane";

    // ────────────────────────────────────────────────────────────────────
    // 1. Lee estado actual del control frame (memory_order_relaxed)
    // ────────────────────────────────────────────────────────────────────
    const auto yamnet_voice = g_control_frame.yamnet_voice_score.load(std::memory_order_relaxed);
    const auto yamnet_music = g_control_frame.yamnet_music_score.load(std::memory_order_relaxed);
    const auto yamnet_bass = g_control_frame.yamnet_bass_score.load(std::memory_order_relaxed);

    const auto eq_gain_db = g_control_frame.eq_gain_db.load(std::memory_order_relaxed);
    const auto comp_threshold_db = g_control_frame.comp_threshold_db.load(std::memory_order_relaxed);
    const auto comp_ratio = g_control_frame.comp_ratio.load(std::memory_order_relaxed);
    const auto exciter_wet = g_control_frame.exciter_wet.load(std::memory_order_relaxed);
    const auto widener_stereo = g_control_frame.widener_stereo.load(std::memory_order_relaxed);

    const auto nho_harmonic = g_control_frame.nho_harmonic_gain.load(std::memory_order_relaxed);
    const auto spatial_angle = g_control_frame.spatial_angle_deg.load(std::memory_order_relaxed);
    const auto spatial_width = g_control_frame.spatial_width.load(std::memory_order_relaxed);
    const auto pd_mode = g_control_frame.pd_mode.load(std::memory_order_relaxed);

    const auto phase_T_refined = g_control_frame.phase_oracle_T_refined.load(std::memory_order_relaxed);
    const auto phase_coherence = g_control_frame.phase_coherence.load(std::memory_order_relaxed);

    const auto audio_engine_exciter = g_control_frame.audio_engine_exciter.load(std::memory_order_relaxed);
    const auto audio_engine_eq = g_control_frame.audio_engine_eq_gain.load(std::memory_order_relaxed);
    const auto audio_engine_width = g_control_frame.audio_engine_width.load(std::memory_order_relaxed);

    const auto evo_active = g_control_frame.evolutionary_active.load(std::memory_order_relaxed);
    const auto anti_dolby = g_control_frame.anti_dolby_enabled.load(std::memory_order_relaxed);

    // ────────────────────────────────────────────────────────────────────
    // 2. Aplicar YAMNet scores → Dynamic widener/EQ ajustment
    // ────────────────────────────────────────────────────────────────────
    // Si hay mucho bass (yamnet_bass > 0.7), reduce widener para claridad
    // Si hay voz (yamnet_voice > 0.6), aplica 2kHz boost
    float yamnet_widener_mult = 1.f;
    float yamnet_eq_boost_2k = 0.f;

    if (yamnet_voice > 0.6f) {
        yamnet_eq_boost_2k = (yamnet_voice - 0.6f) * 3.f;  // +0..1.2 dB
        updates++;
    }
    if (yamnet_bass > 0.7f) {
        yamnet_widener_mult = 0.7f;  // reduce stereo width si hay muchos bajos
        updates++;
    }

    // ────────────────────────────────────────────────────────────────────
    // 3. Aplicar DSP pipeline updates
    // ────────────────────────────────────────────────────────────────────
    // EQ gain + YAMNet boost
    const float combined_eq_gain = eq_gain_db + yamnet_eq_boost_2k + audio_engine_eq;
    g_eq.setGain(combined_eq_gain);
    updates++;

    // Compressor
    g_comp.setParams(comp_threshold_db, comp_ratio);
    updates++;

    // Exciter: fusionar AudioEngine + control frame
    const float combined_exciter = std::min(1.f, exciter_wet + audio_engine_exciter);
    g_exciter.setWet(combined_exciter);
    updates++;

    // Widener: fusionar AudioEngine + YAMNet adjustment
    const float combined_width = (widener_stereo + audio_engine_width) * 0.5f * yamnet_widener_mult;
    g_widener.setWidth(std::clamp(combined_width, 0.f, 1.f));
    updates++;

    // ────────────────────────────────────────────────────────────────────
    // 4. PDEngine mode & phase oracle integration
    // ────────────────────────────────────────────────────────────────────
    g_pd.set_mode_(pd_mode);
    g_pd.set_spatial_angle_(spatial_angle);
    g_pd.set_spatial_width_(spatial_width);
    updates += 3;

    // Inyectar phase oracle prediction en BiquadBank
    if (phase_T_refined > 0.f && phase_coherence > 0.2f) {
        // BiquadBank usa T_refined para refinar envelope timing
        g_pd.cue_bank.set_phase_refinement(phase_T_refined, phase_coherence);
        updates++;
    }

    // ────────────────────────────────────────────────────────────────────
    // 5. NHO harmonic gain
    // ────────────────────────────────────────────────────────────────────
    g_pd.nho.harmonic_gain = nho_harmonic;
    updates++;

    // ────────────────────────────────────────────────────────────────────
    // 6. Evolutionary genome mapping (real-time)
    // ────────────────────────────────────────────────────────────────────
    if (evo_active) {
        // Leer genoma actual
        float evo_dsp[5], evo_nho[4], evo_spatial[3];

        for (int i = 0; i < 5; ++i) {
            evo_dsp[i] = g_control_frame.evo_genome_dsp[i].load(std::memory_order_relaxed);
        }
        for (int i = 0; i < 4; ++i) {
            evo_nho[i] = g_control_frame.evo_genome_nho[i].load(std::memory_order_relaxed);
        }
        for (int i = 0; i < 3; ++i) {
            evo_spatial[i] = g_control_frame.evo_genome_spatial[i].load(std::memory_order_relaxed);
        }

        // Mapeo genome → parámetros reales (ver evolutionary_adapter.hpp)
        // genome[0] → drive (pre-gain DSP)
        // genome[1] → wet (DSP mix)
        // genome[2] → EQ frequency center (100..8k Hz)
        // genome[3] → resonance (Q)
        // genome[4] → harmonic saturation intensity

        float evo_drive = 1.f + evo_dsp[0] * 0.5f;      // 1.0..1.5x
        float evo_eq_freq = 100.f + evo_dsp[2] * 7900.f;  // 100..8k Hz
        float evo_resonance = 0.5f + evo_dsp[3] * 4.5f;  // 0.5..5.0 Q

        // Aplicar a g_exciter drive
        g_exciter.setDrive(evo_drive);

        // Aplicar a g_eq resonancia
        g_eq.setBandwidth(evo_resonance);

        // NHO: genome[5..8] → alpha, beta, mu, harmonic_gain
        g_pd.nho.alpha = 0.5f + evo_nho[0] * 0.4f;      // 0.5..0.9
        g_pd.nho.beta = 0.1f + evo_nho[1] * 0.3f;       // 0.1..0.4
        g_pd.nho.mu = 0.05f + evo_nho[2] * 0.3f;        // 0.05..0.35
        g_pd.nho.harmonic_gain = evo_nho[3];            // 0..1

        // Spatial: genome[9..11] → angle, width, wet
        g_pd.spatial.set_angle_deg(evo_spatial[0] * 90.f);  // 0..90°
        g_pd.spatial.set_width(evo_spatial[1]);             // 0..1
        // evo_spatial[2] → wet mix (reservado para future)

        updates += 3;
    }

    // ────────────────────────────────────────────────────────────────────
    // 7. Anti-Dolby profile application
    // ────────────────────────────────────────────────────────────────────
    if (anti_dolby) {
        // Aplicar perfil Spatial: boost widener + spatial rendering
        g_pd.spatial.set_width(0.8f);  // ancho máximo
        g_pd.set_mode_(2);             // forzar modo +NHO+Spatial
        g_control_frame.spatial_rendering_active.store(true, std::memory_order_relaxed);
        updates++;
    }

    // ────────────────────────────────────────────────────────────────────
    // 8. Debug logging (muy ocasional, no en cada frame)
    // ────────────────────────────────────────────────────────────────────
    static int debug_counter = 0;
    if ((++debug_counter) % 2400 == 0) {  // ~50ms @ 48kHz (cada ~2.4M muestras)
        ALOG(ANDROID_LOG_DEBUG, TAG,
             "ControlPlane: YAMNet(V:%.2f M:%.2f B:%.2f) PDMode=%d EvoActive=%d Updates=%d",
             yamnet_voice, yamnet_music, yamnet_bass, pd_mode, evo_active, updates);
    }

    return updates;
}
