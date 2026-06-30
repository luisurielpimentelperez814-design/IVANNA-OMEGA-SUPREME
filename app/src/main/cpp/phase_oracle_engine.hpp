// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — PhaseOracle (Kalman Cubic Predictor)
 *
 * Filtro de Kalman cinemático de orden 3:
 *   estado: [posición, velocidad, aceleración]
 *   F = [1, dt, dt²/2 ; 0, 1, dt ; 0, 0, 1]
 *
 * Usos dentro del audio thread:
 *   - state[0] = muestra predicha (look-ahead 1 sample)
 *   - state[1] = velocidad → detector de transitorios T_t
 *   - state[2] = aceleración → curvatura de envolvente
 *
 * O(1) por sample. Cero allocations. No STL.
 * ============================================================
 */

#include <cmath>

namespace ivanna {

class PhaseOracle {
public:
    // Estado cinemático
    float pos = 0.f;   // posición (muestra predicha)
    float vel = 0.f;   // velocidad (rate-of-change)
    float acc = 0.f;   // aceleración

    // Covarianza (diagonal simplificada)
    float P0 = 1.f, P1 = 1e4f, P2 = 10.f;

    // Ruido de proceso (ajustable)
    float Q0 = 1e-5f, Q1 = 1e-3f, Q2 = 1e-1f;

    // Ruido de medición
    float R = 0.01f;

    // dt @ 48kHz
    float dt     = 1.f / 48000.f;
    float dt2    = 0.5f * dt * dt;

    void init(float sample_rate) noexcept {
        dt  = 1.f / sample_rate;
        dt2 = 0.5f * dt * dt;
        reset();
    }

    void reset() noexcept {
        pos = vel = acc = 0.f;
        P0 = 1.f; P1 = 1e4f; P2 = 10.f;
    }

    // ── Predict + Update con una medición ────────────────────────────────────
    inline void tick(float measurement) noexcept {
        // Predict
        const float pos_p = pos + dt * vel + dt2 * acc;
        const float vel_p = vel + dt * acc;
        const float acc_p = acc;

        const float P0_p = P0 + Q0;
        const float P1_p = P1 + Q1;
        const float P2_p = P2 + Q2;

        // Update (solo P0 influye en K dado H=[1,0,0])
        const float S     = P0_p + R;
        const float K0    = P0_p / S;
        const float K1    = P1_p / S;   // cruzado aproximado
        const float K2    = P2_p / S;

        const float innov = measurement - pos_p;

        pos = pos_p + K0 * innov;
        vel = vel_p + K1 * innov;
        acc = acc_p + K2 * innov;

        P0 = (1.f - K0) * P0_p;
        P1 = (1.f - K1) * P1_p;
        P2 = (1.f - K2) * P2_p;
    }

    // ── Process un bloque, retorna cue de transitorios ───────────────────────
    // transient_cue = media de |vel| normalizada — alto en ataques/bordes
    float process_block(const float* buf, int n) noexcept {
        float sum_vel = 0.f;
        for (int i = 0; i < n; ++i) {
            tick(buf[i]);
            sum_vel += std::fabs(vel);
        }
        return (n > 0) ? sum_vel / n : 0.f;
    }

    // Predicción de la próxima muestra (look-ahead 1)
    float predict_next() const noexcept {
        return pos + dt * vel + dt2 * acc;
    }

    // Cue de transitorios (|vel| normalizado)
    float transient_cue() const noexcept {
        return std::fabs(vel);
    }

    // Cue de curvatura (|acc|)
    float curvature_cue() const noexcept {
        return std::fabs(acc);
    }
};

} // namespace ivanna
