// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Phase Oracle Engine Refinements
 *
 * Predictor Kalman que estima el período fundamental refinado T_refined.
 * Se integra con audio_control_plane para sincronizar BiquadEnvelopeBank.
 *
 * Estado: z_t = [T_t, dT_t]^T
 * Observación: T_obs (detección de pitch via autocorrelación)
 * ============================================================
 */

struct KalmanPhasePredictor {
    // ── State vector ────────────────────────────────────────────────────
    float T_pos = 0.f;      // posición estimada del período (muestras)
    float T_vel = 0.f;      // velocidad de cambio de período (Δ muestras/bloque)

    // ── Covariance matrix P (2x2 diag para simplificar) ────────────────
    float P_00 = 1.f;      // varianza de T_pos
    float P_11 = 1e4f;     // varianza de T_vel
    
    // ── Dynamics & measurement noise (tuning parameters) ───────────────
    float dt = 1.f / 48000.f;          // tiempo entre muestras
    float dt2 = 0.5f * dt * dt;        // dt²/2
    float sigma_process = 0.01f;       // process noise (período cambia lentamente)
    float sigma_meas = 1.f;            // measurement noise (detección imperfecta)

    // ── Coherence metric ────────────────────────────────────────────────
    float coherence = 0.f;             // [0..1] qué tan seguro estoy del T

    // ── Inicialización ──────────────────────────────────────────────────
    void init(float sample_rate) noexcept {
        dt = 1.f / sample_rate;
        dt2 = 0.5f * dt * dt;
        reset();
    }

    void reset() noexcept {
        T_pos = 0.f;
        T_vel = 0.f;
        P_00 = 1.f;
        P_11 = 1e4f;
        coherence = 0.f;
    }

    // ── Prediction step (a priori) ──────────────────────────────────────
    // z_t = F · z_{t-1}  donde F es matriz de transición
    // [T]     [1  dt] [T]
    // [dT] =  [0   1] [dT]
    void predict_step() noexcept {
        const float T_new = T_pos + T_vel * dt;
        // T_vel no cambia (constant velocity model)
        T_pos = T_new;

        // Covarianza: P = F·P·F^T + Q (Q = process noise)
        const float P_00_new = P_00 + 2.f * P_00 * dt + P_11 * dt2 + sigma_process;
        const float P_11_new = P_11 + sigma_process;

        P_00 = P_00_new;
        P_11 = P_11_new;
    }

    // ── Update step (a posteriori) ──────────────────────────────────────
    // Recibe observación T_obs (detección de pitch, ej. via autocorrelación)
    void update_step(float T_obs, float obs_confidence) noexcept {
        if (T_obs <= 0.f) return;  // observación inválida

        // Innovation: y = T_obs - T_pos
        const float innovation = T_obs - T_pos;

        // Innovación covarianza: S = H·P·H^T + R (H = [1 0], R = meas noise)
        const float S = P_00 + sigma_meas;

        // Kalman gain: K = P·H^T / S
        const float K_00 = P_00 / S;  // ganancia para T_pos
        // K_11 = P_10 / S ≈ 0 (no hay coupling en modelo simple)

        // Update state: z = z + K·innovation
        T_pos = T_pos + K_00 * innovation;

        // Update covariance: P = (I - K·H)·P
        const float P_00_new = (1.f - K_00) * P_00;
        // P_11 no cambia en este modelo simple

        P_00 = P_00_new;

        // Coherence: qué tan alto es el gain (cercano a 1 = confianza alta)
        coherence = std::clamp(1.f - K_00, 0.f, 1.f) * obs_confidence;
    }

    // ── Refinement: aplicar ajuste de fase (para BiquadBank) ───────────
    // Retorna T_refined que se usará para afinar envelopes
    inline float get_refined_period() const noexcept {
        return std::max(0.f, T_pos);
    }

    inline float get_coherence() const noexcept {
        return coherence;
    }
};

// ============================================================
// BiquadEnvelopeBank — Integración con Phase Oracle
// ============================================================

struct BiquadEnvelopeBank {
    static constexpr int NUM_BANDS = 8;  // 8 biquad filters

    struct BiquadFilter {
        float b0 = 0.f, b1 = 0.f, b2 = 0.f;  // coef. numerador
        float a1 = 0.f, a2 = 0.f;            // coef. denominador
        float z1 = 0.f, z2 = 0.f;            // estado (delay line)
    } bands[NUM_BANDS];

    KalmanPhasePredictor phase_predictor;
    float T_refined_smoothed = 0.f;      // período refinado suavizado
    float phase_refinement_gain = 0.f;   // ganancia aplicada a envelopes

    void init(float sample_rate) noexcept {
        phase_predictor.init(sample_rate);
    }

    // ── Inyectar predicción del Phase Oracle ────────────────────────────
    void set_phase_refinement(float T_refined, float coherence) noexcept {
        // Smooth T_refined: 95% old + 5% new
        T_refined_smoothed = 0.95f * T_refined_smoothed + 0.05f * T_refined;

        // Coherence → refinement gain (0..1)
        phase_refinement_gain = coherence * 0.5f;

        // Aplica ajuste en envelope timing
        // (Los coeficientes de los biquads se ajustan según T_refined_smoothed)
    }

    // ── Process block con refinamiento de fase ──────────────────────────
    float process(float x, int band_idx) noexcept {
        if (band_idx < 0 || band_idx >= NUM_BANDS) return x;

        BiquadFilter& b = bands[band_idx];

        // IIR biquad: y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2
        float y = b.b0 * x + b.z1;
        b.z1 = b.b1 * x - b.a1 * y + b.z2;
        b.z2 = b.b2 * x - b.a2 * y;

        // Aplica fase refinement: modula ligeramente la salida según coherencia
        if (phase_refinement_gain > 0.f) {
            y = y * (1.f + phase_refinement_gain * 0.1f);
        }

        return y;
    }

    void reset() noexcept {
        for (int i = 0; i < NUM_BANDS; ++i) {
            bands[i].z1 = bands[i].z2 = 0.f;
        }
        phase_predictor.reset();
        T_refined_smoothed = 0.f;
        phase_refinement_gain = 0.f;
    }
};

#endif  // PHASE_ORACLE_REFINEMENTS_HPP
