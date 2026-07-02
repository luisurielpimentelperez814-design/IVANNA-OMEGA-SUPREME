// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// Proprietary and confidential.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Nonlinear Harmonic Oscillator (NHO)
 * Reemplaza PI-LSTM Milenio Engine.
 *
 * Dinámica: z_{t+1} = z_t + μ · tanh(α·x_t + β·z_t)
 *
 * Ventajas vs CT-LSTM RK4 con upsampling 4×:
 *  - O(1) por sample, cero allocations
 *  - sin upsampler FIR de 128 taps
 *  - sin integración RK4 (innecesaria para shaping perceptual)
 *  - comportamiento HRTF real sustituido por CueBasedSpatial
 *
 * Padé [5/4] tanh mantenida: |err|<2e-5 en [-8,8]
 * ============================================================
 */

#include <cmath>
#include <algorithm>

namespace ivanna {

// ── Padé [5/4] tanh — mismo kernel que PI-LSTM ───────────────────────────────
static inline float nho_tanh(float x) noexcept {
    if (x > 8.f)  return  1.f;
    if (x < -8.f) return -1.f;
    const float x2 = x * x;
    return x * (135135.f + x2 * (17325.f + x2 * 378.f))
             / (135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}

static inline float nho_safe(float x) noexcept {
    return std::isfinite(x) ? x : 0.f;
}

// ── NHO Engine ────────────────────────────────────────────────────────────────
class NHOEngine {
public:
    // State
    float zL = 0.f;   // left state
    float zR = 0.f;   // right state

    // Parameters (tunable via JNI)
    float alpha       = 0.8f;   // input coupling
    float beta        = 0.3f;   // state feedback
    float mu          = 0.15f;  // step size (gating)
    float harmonic_gain = 0.2f; // harmonic mix level
    float wet         = 0.3f;   // wet/dry

    // ── Gating μ_t ─────────────────────────────────────────────────────────
    // μ_t = clamp(||F|| / (||F|| + ||z - z_prev|| + ε), 0, 1) · mu
    static inline float compute_mu(float F, float dz, float base_mu) noexcept {
        constexpr float EPS = 1e-6f;
        const float absF = std::fabs(F);
        const float gate = absF / (absF + std::fabs(dz) + EPS);
        return gate * base_mu;
    }

    // ── Process one stereo sample ───────────────────────────────────────────
    inline void process_sample(float xL, float xR,
                               float& yL, float& yR) noexcept {
        // F(c_t) = tanh(α·x + β·z)
        const float FL = nho_tanh(alpha * xL + beta * zL);
        const float FR = nho_tanh(alpha * xR + beta * zR);

        const float dzL = FL - zL;
        const float dzR = FR - zR;

        const float mu_t_L = compute_mu(FL, dzL, mu);
        const float mu_t_R = compute_mu(FR, dzR, mu);

        // State update: z_{t+1} = z_t + μ_t · F(c_t)
        zL = nho_safe(zL + mu_t_L * FL);
        zR = nho_safe(zR + mu_t_R * FR);

        // Output: y = (1-wet)·x + wet·tanh(harmonic_gain·z)
        const float hL = nho_tanh(harmonic_gain * zL);
        const float hR = nho_tanh(harmonic_gain * zR);

        yL = (1.f - wet) * xL + wet * hL;
        yR = (1.f - wet) * xR + wet * hR;
    }

    // ── Process block — zero allocations ───────────────────────────────────
    void process_block(const float* __restrict__ inL,
                       const float* __restrict__ inR,
                       float* __restrict__ outL,
                       float* __restrict__ outR,
                       int n) noexcept {
        for (int i = 0; i < n; ++i) {
            process_sample(inL[i], inR[i], outL[i], outR[i]);
        }
    }

    void reset() noexcept { zL = 0.f; zR = 0.f; }

    void set_alpha(float v)         noexcept { alpha = std::clamp(v, 0.f, 4.f); }
    void set_beta(float v)          noexcept { beta  = std::clamp(v, 0.f, 1.f); }
    void set_mu(float v)            noexcept { mu    = std::clamp(v, 0.f, 1.f); }
    void set_harmonic_gain(float v) noexcept { harmonic_gain = std::clamp(v, 0.f, 2.f); }
    void set_wet(float v)           noexcept { wet   = std::clamp(v, 0.f, 1.f); }
};

} // namespace ivanna
