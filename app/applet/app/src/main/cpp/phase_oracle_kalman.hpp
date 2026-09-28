// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — PhaseKalman3 (núcleo Kalman de fase)
 *
 * Filtro de Kalman cinemático de orden 3 sobre el estado
 *   x = [posición, velocidad, aceleración]
 * con modelo de aceleración constante y medición = muestra de audio
 * (H = [1, 0, 0]).
 *
 * Este header es la FUENTE ÚNICA DE VERDAD del filtro. Es C++17 puro,
 * sin JNI, sin STL, sin alocaciones, O(1) por muestra — compilable en
 * host (tests) y en Android/NDK (phase_oracle.cpp) con la misma
 * matemática. Los consumidores (biquad_envelope_bank.hpp, JNI) solo
 * leen sus cues vía la firma C phase_oracle_velocity().
 *
 * Corrección de raíz (vs. la implementación previa en phase_oracle.cpp):
 *   1. Predict: P = F·P·Fᵀ + Q completo (matriz 3×3), no diagonal
 *      aproximada. Sin esto P se colapsaba a 0 y el filtro se
 *      congelaba extrapolando el último estado.
 *   2. Update: P = (I − K·H)·P usando la fila 0 vieja para TODAS las
 *      filas (el bug previo usaba P[0][0] ya actualizado en P[1][0]/
 *      P[2][0], dejando la covarianza asimétrica e incorrecta).
 *   3. Simetrización explícita P = (P + Pᵀ)/2 tras cada paso (la
 *      asimetría por redondeo rompe la PSD de P).
 *   4. Guardas de finitud: una muestra NaN/Inf no envenena el estado.
 * ============================================================
 */

#include <cmath>

namespace ivanna {

class PhaseKalman3 {
public:
    // ── Estado cinemático ───────────────────────────────────────────────
    float x[3] = {0.f, 0.f, 0.f};   // [pos, vel, acc]

    // ── Covarianza (matriz 3×3 simétrica) ──────────────────────────────
    float P[3][3] = {};

    // ── Ruido de proceso (diagonal; ajustable por componente) ──────────
    float Q[3][3] = {};

    // ── Ruido de medición ──────────────────────────────────────────────
    float R = 0.01f;

    // ── Dinámica @ sample_rate ─────────────────────────────────────────
    float dt  = 1.f / 48000.f;
    float dt2 = 0.5f * dt * dt;

    void init(float sample_rate) noexcept {
        dt  = (sample_rate > 0.f) ? 1.f / sample_rate : 1.f / 48000.f;
        dt2 = 0.5f * dt * dt;
        reset();
    }

    void reset() noexcept {
        x[0] = x[1] = x[2] = 0.f;
        const float diag[3] = {1.f, 1e4f, 10.f};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) P[i][j] = 0.f;
            P[i][i] = diag[i];
        }
        const float qdiag[3] = {1e-5f, 1e-3f, 1e0f};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) Q[i][j] = 0.f;
            Q[i][i] = qdiag[i];
        }
        // R calibrado con evidencia (repro host de 440 Hz, 48 kHz):
        //   R=1e-2,Q2=1e-1 → error de predicción 89% de la amplitud
        //   R=1e-4,Q2=1e0  → error 21%  (sigue la onda; medido, no supuesto)
        R = 1e-4f;
    }

    // ── Predict: x = F·x ;  P = F·P·Fᵀ + Q ──────────────────────────────
    void predict() noexcept {
        const float d = dt, d2 = dt2;
        const float x0 = x[0], x1 = x[1], x2 = x[2];
        x[0] = x0 + d * x1 + d2 * x2;
        x[1] = x1 + d * x2;
        // x[2] sin cambio (modelo de aceleración constante)

        // FP = F·P  (F = [[1, d, d2],[0, 1, d],[0, 0, 1]])
        float FP[3][3];
        for (int j = 0; j < 3; ++j) {
            const float p0j = P[0][j], p1j = P[1][j], p2j = P[2][j];
            FP[0][j] = p0j + d * p1j + d2 * p2j;
            FP[1][j] = p1j + d * p2j;
            FP[2][j] = p2j;
        }
        // P = FP·Fᵀ + Q, por columnas: col j = FP[i] · F[j] (fila j de F):
        //   F[0] = [1,  d, d2] → col0 = FPi0 + d·FPi1 + d2·FPi2
        //   F[1] = [0,  1,  d] → col1 = FPi1 + d·FPi2
        //   F[2] = [0,  0,  1] → col2 = FPi2 + Q
        for (int i = 0; i < 3; ++i) {
            const float FPi0 = FP[i][0], FPi1 = FP[i][1], FPi2 = FP[i][2];
            float* Pi = P[i];
            Pi[0] = FPi0 + d * FPi1 + d2 * FPi2 + Q[i][0];
            Pi[1] = FPi1 + d * FPi2 + Q[i][1];
            Pi[2] = FPi2 + Q[i][2];
        }
        symmetrize();
    }

    // ── Update: y = z − H·x ;  K = P·Hᵀ/S ;  x += K·y ;  P = (I−K·H)·P ─
    void update(float z) noexcept {
        if (!std::isfinite(z)) return;          // muestra inválida: no envenenar
        const float y = z - x[0];
        const float S = P[0][0] + R;
        if (S <= 0.f) return;
        const float inv = 1.f / S;

        float K[3];
        for (int i = 0; i < 3; ++i) K[i] = P[i][0] * inv;

        for (int i = 0; i < 3; ++i) x[i] += K[i] * y;

        // P = (I − K·H)·P  →  P_new[i][j] = P[i][j] − K[i]·P[0][j]
        // (usar la fila 0 VIEJA para todas las filas — el bug previo
        //  usaba P[0][0] ya actualizado en las filas 1 y 2)
        const float P00 = P[0][0], P01 = P[0][1], P02 = P[0][2];
        for (int i = 0; i < 3; ++i) {
            const float Ki = K[i];
            float* Pi = P[i];
            Pi[0] = Pi[0] - Ki * P00;
            Pi[1] = Pi[1] - Ki * P01;
            Pi[2] = Pi[2] - Ki * P02;
        }
        symmetrize();
    }

    // ── Predict + Update con una medición ───────────────────────────────
    inline void tick(float z) noexcept {
        predict();
        update(z);
    }

    // ── Cues perceptuales (consumidores DSP) ────────────────────────────
    float predict_next() const noexcept {
        return x[0] + dt * x[1] + dt2 * x[2];
    }

    // |velocidad| — detector de transitorios O(1) por muestra
    float transient_cue() const noexcept {
        return std::fabs(x[1]);
    }

    // |aceleración| — curvatura de envolvente
    float curvature_cue() const noexcept {
        return std::fabs(x[2]);
    }

    // Ganancia efectiva K0 = P00/(P00+R) — proxy de "cuánto confía el
    // filtro en la medición". Debe quedar en (0,1) y NO colapsar a 0
    // (el bug previo congelaba el filtro: K→0 tras suficientes bloques).
    float gain() const noexcept {
        const float S = P[0][0] + R;
        return (S > 0.f) ? P[0][0] / S : 0.f;
    }

private:
    void symmetrize() noexcept {
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j) {
                const float m = 0.5f * (P[i][j] + P[j][i]);
                P[i][j] = P[j][i] = m;
            }
    }
};

} // namespace ivanna
