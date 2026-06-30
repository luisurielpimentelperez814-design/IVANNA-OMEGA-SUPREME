// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Cue-Based Spatial Engine
 * Reemplaza Ω-Atlas Spatial (room_model + HRTF físico falso).
 *
 * Modelo perceptual real basado en cues biaurales:
 *
 *   ITD(θ) = τ_max · sin(θ)          (Interaural Time Difference)
 *   ILD(θ) = α_ild · sin(θ)          (Interaural Level Difference)
 *
 * Donde τ_max ≈ 0.65ms (cabeza esférica, radio 8.75cm)
 * y α_ild controla la asimetría de nivel.
 *
 * Implementación:
 *  - ITD → fractional delay 1 sample máx (a 48kHz, 0.65ms ≈ 31 samples)
 *    simplificado a delay entero + interpolación lineal
 *  - ILD → multiplicación directa por cos/sin de θ
 *  - Sin room simulation, sin delay lines de 512+ samples
 *  - O(1) por sample, cero allocations
 * ============================================================
 */

#include <cmath>
#include <algorithm>

namespace ivanna {

static constexpr float SPATIAL_PI    = 3.14159265f;
static constexpr float TAU_MAX_SAMP  = 31.f;   // 0.65ms @ 48kHz
static constexpr float ILD_ALPHA     = 0.3f;   // nivel max L-R ±30%
static constexpr int   ITD_BUF_SIZE  = 64;     // power-of-2, ≥ TAU_MAX_SAMP+1

class CueBasedSpatial {
public:
    float theta   = 0.f;  // ángulo en radianes [-PI/2, PI/2]
    float width   = 1.f;  // 0=mono, 1=full stereo
    float wet     = 0.5f;

    // ITD delay buffer (left channel delayed when source is right)
    float delayBufL[ITD_BUF_SIZE] = {};
    float delayBufR[ITD_BUF_SIZE] = {};
    int   writeIdx = 0;

    void reset() noexcept {
        for (int i = 0; i < ITD_BUF_SIZE; ++i) delayBufL[i] = delayBufR[i] = 0.f;
        writeIdx = 0;
    }

    // ── Compute ITD delay in samples for given angle ──────────────────────
    // Positive θ = source to the right → left ear delayed
    float itd_samples(float sample_rate) const noexcept {
        const float tau_sec = 0.00065f * std::sin(theta);
        return tau_sec * sample_rate;
    }

    // ── ILD gains ─────────────────────────────────────────────────────────
    void ild_gains(float& gainL, float& gainR) const noexcept {
        const float s = ILD_ALPHA * std::sin(theta) * width;
        gainL = 1.f - s;   // attenuate ipsilateral
        gainR = 1.f + s;   // boost contralateral
        gainL = std::clamp(gainL, 0.3f, 1.5f);
        gainR = std::clamp(gainR, 0.3f, 1.5f);
    }

    // ── Process one stereo sample ─────────────────────────────────────────
    void process_sample(float xL, float xR,
                        float& yL, float& yR,
                        float sample_rate = 48000.f) noexcept {
        // Write into circular buffer
        delayBufL[writeIdx & (ITD_BUF_SIZE-1)] = xL;
        delayBufR[writeIdx & (ITD_BUF_SIZE-1)] = xR;

        // ITD: delay amount (fractional, interpolated)
        const float delay = std::clamp(itd_samples(sample_rate), 0.f, TAU_MAX_SAMP);
        const int   d0    = (int)delay;
        const float frac  = delay - (float)d0;

        // Read delayed versions
        const int idxA_L = (writeIdx - d0)     & (ITD_BUF_SIZE-1);
        const int idxB_L = (writeIdx - d0 - 1) & (ITD_BUF_SIZE-1);
        const int idxA_R = (writeIdx - d0)     & (ITD_BUF_SIZE-1);
        const int idxB_R = (writeIdx - d0 - 1) & (ITD_BUF_SIZE-1);

        // Source right → delay left; source left → delay right
        float dL, dR;
        if (theta >= 0.f) {
            // Source right: delay left ear
            dL = delayBufL[idxA_L] * (1.f-frac) + delayBufL[idxB_L] * frac;
            dR = xR;
        } else {
            // Source left: delay right ear
            dL = xL;
            dR = delayBufR[idxA_R] * (1.f-frac) + delayBufR[idxB_R] * frac;
        }

        // ILD
        float gL, gR;
        ild_gains(gL, gR);
        dL *= gL;
        dR *= gR;

        // Wet/dry mix
        yL = (1.f - wet) * xL + wet * dL;
        yR = (1.f - wet) * xR + wet * dR;

        ++writeIdx;
    }

    // ── Process block ─────────────────────────────────────────────────────
    void process_block(const float* inL, const float* inR,
                       float* outL, float* outR,
                       int n, float sample_rate = 48000.f) noexcept {
        for (int i = 0; i < n; ++i) {
            process_sample(inL[i], inR[i], outL[i], outR[i], sample_rate);
        }
    }

    void set_angle_deg(float deg) noexcept {
        theta = std::clamp(deg, -90.f, 90.f) * (SPATIAL_PI / 180.f);
    }
    void set_width(float w) noexcept { width = std::clamp(w, 0.f, 2.f); }
    void set_wet(float w)   noexcept { wet   = std::clamp(w, 0.f, 1.f); }
};

} // namespace ivanna
