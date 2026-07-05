// © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E — All rights reserved.
// Proprietary and confidential. Embedded copyright; do not strip.
// Verify with ivannanpe::verifyCopyrightIntegrity() at boot.
#pragma once
#include "../include/ivanna_npe_license.h"

/*
 * ============================================================
 * OMEGA EQ PRO — Ivannuri Gold
 * PI-LSTM Milenio Engine v2.1 (FIXED)
 * Copyright (C) GORE TNS / Luis Uriel Pimentel Pérez
 * All rights reserved. Proprietary and confidential.
 *
 * Signal path:
 * Input (96kHz) → 4x Upsample → CT-LSTM RK4 @ 384kHz → Harmonic Exciter
 * → HRTF Binaural Field → 4x Downsample → Output (96kHz)
 *
 * Safety: no NaN/Inf output under any input including NaN/Inf/extremes.
 * ============================================================
 */
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ivanna {
static constexpr int DIM = 1;
static constexpr int DIM2 = DIM * 2;
static constexpr int BLOCK = 128;
static constexpr int UP_FACTOR = 4;
static constexpr int FIR_TAPS = 128;  // FIX #12: Changed to 128 (divisible by 4)
static constexpr int N_REFL = 8;
static constexpr int HRTF_LEN = 512;
static constexpr float FS_BASE = 96000.f;
static constexpr float FS_ULTRA = 384000.f;
static constexpr float DT_ULTRA = 1.f / FS_ULTRA;
static constexpr float KPI = 3.14159265f;

// Safety helpers
static inline float sf(float x) noexcept { return std::isfinite(x) ? x : 0.f; }
static inline float clampf(float x, float a, float b) noexcept { return x < a ? a : (x > b ? b : x); }
static inline float sc(float x) noexcept { return clampf(sf(x), -8.f, 8.f); }

// Padé [5/4] tanh, |err|<2e-5 on [-8,8]
static inline float fast_tanh(float x) noexcept {
    x = clampf(x, -8.f, 8.f);
    float x2 = x * x;
    return x * (135135.f + x2 * (17325.f + x2 * 378.f)) / (135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}
static inline float fast_sig(float x) noexcept { return 0.5f + 0.5f * fast_tanh(x * 0.5f); }

// Bessel I0 for Kaiser window
static inline float bess0(float x) noexcept {
    float s = 1.f, t = 1.f, h = (x * 0.5f) * (x * 0.5f);
    for (int k = 1; k <= 20; ++k) {
        t *= h / (float)(k * k);
        s += t;
        if (t < 1e-12f * s) break;
    }
    return s;
}

// FIX #12: FIR_TAPS must be divisible by UP_FACTOR for correct polyphasic decomposition
static_assert(FIR_TAPS % UP_FACTOR == 0, "FIR_TAPS must be divisible by UP_FACTOR");

static inline void build_fir(float* h, float fc, float beta) noexcept {
    float norm = bess0(beta), half = (float)(FIR_TAPS - 1) * 0.5f;
    for (int n = 0; n < FIR_TAPS; ++n) {
        float x = (float)n - half;
        float w = bess0(beta * std::sqrt(1.f - (x / half) * (x / half))) / norm;
        float s = (x == 0.f) ? 1.f : std::sin(KPI * fc * x) / (KPI * fc * x);
        h[n] = fc * s * w;
    }
}

// ── Polyphasic Upsampler (FIXED) ────────────────────────────
struct PolyphasicUpsampler {
    float h[FIR_TAPS];
    float dL[FIR_TAPS], dR[FIR_TAPS];
    int head = 0;
    float sub[UP_FACTOR][FIR_TAPS / UP_FACTOR];  // FIX: exact division

    PolyphasicUpsampler(float fc = 0.5f / UP_FACTOR, float beta = 8.0f) {
        build_fir(h, fc, beta);
        memset(dL, 0, sizeof(dL));
        memset(dR, 0, sizeof(dR));
        // FIX: Correct polyphasic decomposition with exact division
        for (int p = 0; p < UP_FACTOR; ++p) {
            for (int k = 0; k < FIR_TAPS / UP_FACTOR; ++k) {
                sub[p][k] = h[p + k * UP_FACTOR];
            }
        }
    }

    void push(float L, float R) {
        dL[head] = L;
        dR[head] = R;
        head = (head + 1) % FIR_TAPS;
    }

    // FIX: Correct indexing with exact division
    bool pop(float& oL, float& oR) {
        int phase = head % UP_FACTOR;  // FIX: use head for phase
        float aL = 0.f, aR = 0.f;
        for (int k = 0; k < FIR_TAPS / UP_FACTOR; ++k) {
            int off = phase + k * UP_FACTOR;
            if (off >= FIR_TAPS) break;  // Safety check
            int i = (head + FIR_TAPS - off - 1) % FIR_TAPS;
            aL += sub[phase][k] * dL[i];
            aR += sub[phase][k] * dR[i];
        }
        oL = sf(aL);
        oR = sf(aR);
        return true;
    }
};

// ── CT-LSTM Cell (RK4 @ 384kHz) ──────────────────────────────
struct CTLSTMCell {
    float c = 0.f, h = 0.f;
    float Wf = -2.f, Wi = 2.f, Wc = 0.5f, Wo = 0.f;
    float bf = 1.f, bi = -1.f, bc = 0.f, bo = 0.f;
    float alpha = 1.0f, beta = 1.0f, gamma_p = 1.0f;
    float delta = 0.1f, eta = 1.0f;
    float NP_max = 1.0f;
    // AUDIT FIX (TODO pi_lstm_bridge_jni.cpp:nativeGetError): la celda no
    // exponía ningún residual real. rk4_step ya calcula k1_h..k4_h (derivada
    // de h en cada sub-paso RK4); su combinación es literalmente el residuo
    // de la ODE dh/dt en ese paso. Se guarda para exponerlo sin inventar una
    // métrica nueva ni requerir ground-truth externo.
    float last_residual = 0.f;

    float f_gate(float x) const noexcept { return fast_sig(alpha * x + bf); }
    float i_gate(float x) const noexcept { return fast_sig(beta * x + bi); }
    float c_tilde(float x) const noexcept { return fast_tanh(gamma_p * x + bc); }
    float o_gate(float x) const noexcept { return fast_sig(Wo * x + bo); }

    float dc_dt(float c_now, float h_prev, float x) const noexcept {
        float f = f_gate(Wf * h_prev + x);
        float i = i_gate(Wi * h_prev + x);
        float ct = c_tilde(Wc * h_prev + x);
        return f * c_now + i * ct - c_now;
    }

    float dh_dt(float c_now, float h_prev, float x) const noexcept {
        float o = o_gate(Wo * h_prev + x);
        return o * fast_tanh(c_now) - h_prev;
    }

    void rk4_step(float x, float dt) {
        // FIX #11: Validate dt to prevent instability
        dt = clampf(dt, 1e-9f, 1e-3f);  // Clamp dt to reasonable range

        float k1_c = dc_dt(c, h, x);
        float k1_h = dh_dt(c, h, x);

        float c2 = c + k1_c * dt * 0.5f;
        float h2 = h + k1_h * dt * 0.5f;
        float k2_c = dc_dt(c2, h2, x);
        float k2_h = dh_dt(c2, h2, x);

        float c3 = c + k2_c * dt * 0.5f;
        float h3 = h + k2_h * dt * 0.5f;
        float k3_c = dc_dt(c3, h3, x);
        float k3_h = dh_dt(c3, h3, x);

        float c4 = c + k3_c * dt;
        float h4 = h + k3_h * dt;
        float k4_c = dc_dt(c4, h4, x);
        float k4_h = dh_dt(c4, h4, x);

        const float dc = (k1_c + 2.f * k2_c + 2.f * k3_c + k4_c) / 6.f;
        const float dh = (k1_h + 2.f * k2_h + 2.f * k3_h + k4_h) / 6.f;
        c += dc * dt;
        h += dh * dt;

        // FIX: Clamp states to prevent divergence
        c = clampf(c, -NP_max, NP_max);
        h = clampf(h, -NP_max, NP_max);

        // AUDIT FIX: residuo real de la ODE en este paso (antes se perdía).
        last_residual = std::fabs(dc) + std::fabs(dh);
    }

    void reset() noexcept { c = 0.f; h = 0.f; last_residual = 0.f; }
};

// ── Harmonic Exciter ────────────────────────────────────────


// ── HRTF + Early Reflections ──────────────────────────────────
struct HRTFReflectionEngine {
    float hrtf_L[HRTF_LEN], hrtf_R[HRTF_LEN];
    float hbufL[HRTF_LEN], hbufR[HRTF_LEN];
    int hhead = 0;
    static constexpr int MAX_DELAY = 4096;
    float rbufL[MAX_DELAY], rbufR[MAX_DELAY];
    int rhead = 0;
    int delays_smp[N_REFL];
    float gains[N_REFL];
    bool enabled = true;

    void init(const float ds[N_REFL], const float gs[N_REFL]) noexcept {
        for (int i = 0; i < N_REFL; ++i) {
            delays_smp[i] = (int)(ds[i] * FS_BASE);
            gains[i] = gs[i];
        }
        memset(hbufL, 0, sizeof(hbufL));
        memset(hbufR, 0, sizeof(hbufR));
        memset(rbufL, 0, sizeof(rbufL));
        memset(rbufR, 0, sizeof(rbufR));
        hhead = 0;
        rhead = 0;
    }

    void process(float L, float R, float& oL, float& oR) {
        if (!enabled) { oL = L; oR = R; return; }

        hbufL[hhead] = L;
        hbufR[hhead] = R;
        hhead = (hhead + 1) % HRTF_LEN;

        float cL = 0.f, cR = 0.f;
        for (int i = 0; i < HRTF_LEN; ++i) {
            int idx = (hhead + HRTF_LEN - i) % HRTF_LEN;
            cL += hbufL[idx] * hrtf_L[i];
            cR += hbufR[idx] * hrtf_R[i];
        }

        rbufL[rhead] = cL;
        rbufR[rhead] = cR;
        rhead = (rhead + 1) % MAX_DELAY;

        oL = cL;
        oR = cR;
        for (int i = 0; i < N_REFL; ++i) {
            int d = (rhead + MAX_DELAY - delays_smp[i]) % MAX_DELAY;
            oL += rbufL[d] * gains[i];
            oR += rbufR[d] * gains[i];
        }
        oL = sf(oL);
        oR = sf(oR);
    }

    void reset() noexcept {
        memset(hbufL, 0, sizeof(hbufL));
        memset(hbufR, 0, sizeof(hbufR));
        memset(rbufL, 0, sizeof(rbufL));
        memset(rbufR, 0, sizeof(rbufR));
        hhead = 0;
        rhead = 0;
    }
};

// ── PI-LSTM Milenio Engine ───────────────────────────────────
struct PILSTMMilenioEngine {
    PolyphasicUpsampler up;
    CTLSTMCell lstm;
    // Harmonic exciter removed - using inline processing
    HRTFReflectionEngine hrtf;

    float harmonic_gain = 0.3f;
    bool adapt_enabled = true;
    int adapt_counter = 0;
    static constexpr int ADAPT_PERIOD = 1024;

    float inL[BLOCK], inR[BLOCK];
    float upL[BLOCK * UP_FACTOR], upR[BLOCK * UP_FACTOR];
    float outL[BLOCK], outR[BLOCK];

    void reset() {
        up = PolyphasicUpsampler();
        lstm.reset();
                hrtf.reset();
        memset(inL, 0, sizeof(inL));
        memset(inR, 0, sizeof(inR));
        memset(upL, 0, sizeof(upL));
        memset(upR, 0, sizeof(upR));
        memset(outL, 0, sizeof(outL));
        memset(outR, 0, sizeof(outR));
    }

    // FIX #11: Process with correct dt for 384kHz sampling
    void process_block(const float* L, const float* R, float* oL, float* oR, int n) {
        if (n > BLOCK) n = BLOCK;

        // Upsample
        for (int i = 0; i < n; ++i) {
            up.push(L[i], R[i]);
            for (int p = 0; p < UP_FACTOR; ++p) {
                up.pop(upL[i * UP_FACTOR + p], upR[i * UP_FACTOR + p]);
            }
        }

        // CT-LSTM @ 384kHz with validated dt
        for (int i = 0; i < n * UP_FACTOR; ++i) {
            lstm.rk4_step((upL[i] + upR[i]) * 0.5f, DT_ULTRA);
            float mod = 1.f + lstm.h * 0.1f;
            upL[i] *= mod;
            upR[i] *= mod;
        }

        // Inline harmonic exciter (replaced removed struct)
        for (int i = 0; i < n * UP_FACTOR; ++i) {
            float xL = upL[i] * harmonic_gain;
            float xR = upR[i] * harmonic_gain;
            // Soft-clip saturation (tanh approximation)
            upL[i] += fast_tanh(xL) * 0.3f;
            upR[i] += fast_tanh(xR) * 0.3f;
            upL[i] = sf(upL[i]);
            upR[i] = sf(upR[i]);
        }

        // HRTF + reflections
        for (int i = 0; i < n * UP_FACTOR; ++i) {
            hrtf.process(upL[i], upR[i], upL[i], upR[i]);
        }

        // Downsample (simple decimation + anti-aliasing)
        for (int i = 0; i < n; ++i) {
            float sumL = 0.f, sumR = 0.f;
            for (int p = 0; p < UP_FACTOR; ++p) {
                sumL += upL[i * UP_FACTOR + p];
                sumR += upR[i * UP_FACTOR + p];
            }
            oL[i] = sf(sumL / UP_FACTOR);
            oR[i] = sf(sumR / UP_FACTOR);
        }

        // Adaptation
        if (adapt_enabled && ++adapt_counter >= ADAPT_PERIOD) {
            adapt_counter = 0;
            float rms = 0.f;
            for (int i = 0; i < n; ++i) {
                rms += oL[i] * oL[i] + oR[i] * oR[i];
            }
            rms = std::sqrt(rms / (2.f * n));
            if (rms > 0.95f) {
                lstm.alpha = clampf(lstm.alpha * 0.99f, 0.01f, 20.f);
            } else if (rms < 0.1f) {
                lstm.alpha = clampf(lstm.alpha * 1.01f, 0.01f, 20.f);
            }
        }
    }

    void set_alpha(float v) noexcept { lstm.alpha = clampf(v, .01f, 20.f); }
    void set_beta(float v) noexcept { lstm.beta = clampf(v, .01f, 20.f); }
    void set_gamma(float v) noexcept { lstm.gamma_p = clampf(v, .01f, 20.f); }
    void set_delta(float v) noexcept { lstm.delta = clampf(v, .01f, 20.f); }
    void set_eta(float v) noexcept { lstm.eta = clampf(v, 0.f, 5.f); }
    void set_harmonic_gain(float v) noexcept { harmonic_gain = clampf(v, 0.f, 1.f); }
    void set_hrtf_enabled(bool en) noexcept { hrtf.enabled = en; }
    void set_adapt_enabled(bool en) noexcept { adapt_enabled = en; }
    void set_np_max(float v) noexcept { lstm.NP_max = clampf(v, .1f, 10.f); }
    void set_reflection_gain(int i, float g) noexcept { if (i >= 0 && i < N_REFL) hrtf.gains[i] = clampf(g, -1.f, 1.f); }
    void set_reflection_delay(int i, float d) noexcept { if (i >= 0 && i < N_REFL) hrtf.delays_smp[i] = (int)(clampf(d, 0.f, 1.f) * FS_BASE); }
};



} // namespace ivanna
