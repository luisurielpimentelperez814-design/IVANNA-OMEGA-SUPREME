// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * BiquadEnvelopeBank — 8 bandas IIR O(1) para extracción de cues perceptuales
 * Reemplaza NeuroCochlear Manifold + Volterra H2 O(K²)
 *
 * NOTA: usa BandBiquad (float, Direct Form II Transposed) distinto del
 * ivanna::Biquad de dsp_types.h (double, Direct Form I) para evitar colisión.
 *
 * INTEGRACIÓN PhaseOracle:
 *   phase_oracle_velocity() (state[1] del KalmanCúbico) es un detector de
 *   transitorios O(1) por sample. Se mezcla con el transient IIR:
 *     T_refined = lerp(T_iir, T_kalman, KALMAN_BLEND)
 *   donde KALMAN_BLEND=0.35 — el Kalman refina sin dominar.
 */

#include <cmath>
#include <algorithm>
#include <cstdint>

// C linkage to phase_oracle.cpp — zero overhead, same TU visibility as JNI
extern "C" float phase_oracle_velocity();

namespace ivanna {

static constexpr float KALMAN_BLEND = 0.35f;  // PhaseOracle weight in T_refined

static constexpr int BEB_BANDS = 8;
static constexpr float BEB_FREQS[BEB_BANDS] = {
    80.f, 200.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f
};

// ── BandBiquad: float, Direct Form II Transposed ──────────────────────────────
struct BandBiquad {
    float b0=1, b1=0, b2=0, a1=0, a2=0;
    float s1=0, s2=0;

    void set_bandpass(float fc, float fs, float Q = 1.5f) noexcept {
        const float w0    = 2.f * 3.14159265f * fc / fs;
        const float sinw  = std::sin(w0);
        const float cosw  = std::cos(w0);
        const float alpha = sinw / (2.f * Q);
        const float inv   = 1.f / (1.f + alpha);
        b0 =  (sinw * 0.5f) * inv;
        b1 =  0.f;
        b2 = -b0;
        a1 = -2.f * cosw * inv;
        a2 = (1.f - alpha) * inv;
    }

    inline float tick(float x) noexcept {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { s1 = s2 = 0.f; }
};

// ── Cues perceptuales ─────────────────────────────────────────────────────────
struct PerceptualCues {
    float L = 0.f;   // loudness
    float T = 0.f;   // transients
    float S = 0.f;   // spatial (L-R diff)
    float R = 0.f;   // residual texture (high bands)
};

// ── BiquadEnvelopeBank ────────────────────────────────────────────────────────
class BiquadEnvelopeBank {
public:
    BandBiquad filtersL[BEB_BANDS];
    BandBiquad filtersR[BEB_BANDS];

    float envL[BEB_BANDS]     = {};
    float envR[BEB_BANDS]     = {};
    float prevEnvL[BEB_BANDS] = {};

    float env_attack  = 0.001f;
    float env_release = 0.010f;

    void init(uint32_t sr) noexcept {
        for (int b = 0; b < BEB_BANDS; ++b) {
            filtersL[b].set_bandpass(BEB_FREQS[b], (float)sr);
            filtersR[b].set_bandpass(BEB_FREQS[b], (float)sr);
        }
        reset();
    }

    void reset() noexcept {
        for (int b = 0; b < BEB_BANDS; ++b) {
            filtersL[b].reset(); filtersR[b].reset();
            envL[b] = envR[b] = prevEnvL[b] = 0.f;
        }
    }

    PerceptualCues extract(float xL, float xR) noexcept {
        float loudness = 0.f, transient = 0.f, residual = 0.f, lr_diff = 0.f;

        for (int b = 0; b < BEB_BANDS; ++b) {
            const float bL = filtersL[b].tick(xL);
            const float bR = filtersR[b].tick(xR);
            const float eL = std::fabs(bL);
            const float eR = std::fabs(bR);

            prevEnvL[b] = envL[b];
            envL[b] += ((eL > envL[b]) ? env_attack : env_release) * (eL - envL[b]);
            envR[b] += ((eR > envR[b]) ? env_attack : env_release) * (eR - envR[b]);

            loudness  += envL[b] + envR[b];
            transient += std::max(0.f, envL[b] - prevEnvL[b]);
            if (b >= 5) residual += envL[b] + envR[b];
            lr_diff   += std::fabs(envL[b] - envR[b]);
        }

        constexpr float INV = 1.f / (2.f * BEB_BANDS);

        // PhaseOracle refinement of transient cue:
        // state[1] (velocity = audio derivative) is a faster, O(1) transient
        // detector that bypasses envelope attack lag. Blend at KALMAN_BLEND.
        const float vel = phase_oracle_velocity();
        const float abs_vel = vel < 0.f ? -vel : vel;
        // Soft-normalize: empirical peak ~5000 at 48kHz, ±1 signal
        const float x = abs_vel * (1.0f / 5000.0f);
        const float x2 = x * x;
        const float t_kalman = x * (27.f + x2) / (27.f + 9.f * x2);  // fast tanh
        const float t_iir = transient * INV * 4.f;
        const float t_refined = t_iir * (1.f - KALMAN_BLEND) + t_kalman * KALMAN_BLEND;

        return { loudness*INV, t_refined, lr_diff*INV, residual/6.f };
    }

    PerceptualCues process_block(const float* inL, const float* inR, int n) noexcept {
        PerceptualCues acc{};
        for (int i = 0; i < n; ++i) {
            const PerceptualCues c = extract(inL[i], inR[i]);
            acc.L += c.L; acc.T += c.T; acc.S += c.S; acc.R += c.R;
        }
        const float inv = (n > 0) ? 1.f/n : 0.f;
        return { acc.L*inv, acc.T*inv, acc.S*inv, acc.R*inv };
    }
};

} // namespace ivanna
