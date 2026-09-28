// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#ifndef IVANNA_PHASE_ORACLE_REFINEMENTS_HPP
#define IVANNA_PHASE_ORACLE_REFINEMENTS_HPP

// AUDITORÍA (flanco PhaseOracle, 2026-09-07): antes el archivo terminaba
// en un `#endif  // PHASE_ORACLE_REFINEMENTS_HPP` SIN ningún `#ifndef` que
// lo abriera — la macro nunca se definía y el primer include del header
// reventaba la compilación. Ahora es un guard doble correcto.
// Además se eliminó el `struct BiquadEnvelopeBank` duplicado que había
// aquí: colisionaba con la clase real `ivanna::BiquadEnvelopeBank` de
// neuromorphic/biquad_envelope_bank.hpp (dos implementaciones del mismo
// concepto — prohibido por el criterio del repo). Este header queda solo
// con KalmanPhasePredictor, que es lo único propio de refinamientos.

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
    float dt = 1.f / 96000.f;          // tiempo entre muestras
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

        // Covarianza: P = F·P·F^T + Q con F = [[1, dt],[0, 1]]:
        //   P00' = P00 + 2·dt·P01 + dt²·P11 + Q00   (P01=0 en este modelo diag)
        //   P11' = P11 + Q11
        // (el término `2·P00·dt` previo era espurio: el único cruce de F
        //  entra por P01; corregido a la raíz del cuadrado real)
        const float P_00_new = P_00 + dt * dt * P_11 + sigma_process;
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

#endif  // IVANNA_PHASE_ORACLE_REFINEMENTS_HPP
