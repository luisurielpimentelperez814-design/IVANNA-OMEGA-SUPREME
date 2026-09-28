// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// test_phase_oracle_kalman.cpp — suite host del núcleo Kalman de fase
// (PhaseKalman3, phase_oracle_kalman.hpp). Verifica propiedades REALES,
// no solo que compila:
//   1. Seguimiento de seno: error de predicción acotado tras warmup.
//   2. Covarianza simétrica PSD (P == Pᵀ, diagonal ≥ 0).
//   3. Cero NaN/Inf en 1M muestras (incl. muestras NaN en la entrada).
//   4. Cue de transitorio: sube en ataques y decae en silencio.
//   5. Ganancia de Kalman en (0,1) y NO colapsada (el bug previo
//      congelaba el filtro: K→0 y dejaba de seguir mediciones nuevas).
//   6. predict_next() = look-ahead 1 consistente con el estado.
//
// Compilación: vía ivanna_add_test() en tests/CMakeLists.txt (GTest
// vendoreado, offline, C++17, sin JNI, sin Android).

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "../phase_oracle_kalman.hpp"

namespace {

using ivanna::PhaseKalman3;

constexpr float kFs = 48000.f;

// ── 1. Seguimiento de seno ──────────────────────────────────────────────
TEST(PhaseKalman3, TracksSineWithBoundedPredictionError) {
    PhaseKalman3 k;
    k.init(kFs);

    constexpr float kFreq = 440.f;
    constexpr float kAmp  = 0.5f;
    constexpr int   kWarm = 4000;   // ~83 ms de convergencia
    constexpr int   kTest = 8000;

    double phase = 0.0;
    const double step = 2.0 * M_PI * kFreq / kFs;

    for (int i = 0; i < kWarm; ++i) {
        k.tick(static_cast<float>(kAmp * std::sin(phase)));
        phase += step;
    }

    // Tras warmup, la predicción a 1 muestra debe seguir la onda con
    // error mucho menor que la amplitud (el filtro "aprende" la onda).
    double maxErr = 0.0;
    for (int i = 0; i < kTest; ++i) {
        const float z = static_cast<float>(kAmp * std::sin(phase));
        const float pred = k.predict_next();
        const float err = std::fabs(pred - z);
        maxErr = std::max(maxErr, static_cast<double>(err));
        k.tick(z);
        phase += step;
    }

    // Umbral con evidencia: con los defaults calibrados (R=1e-4, Q2=1e0)
    // el error máximo medido en repro host es ~21% de la amplitud;
    // se usa 25% para dejar margen de toolchain sin perder significado
    // (con los defaults viejos el error era ~89% → el test fallaría).
    EXPECT_LT(maxErr, 0.25 * kAmp)
        << "maxErr=" << maxErr << " — el filtro no sigue la onda";
}

// ── 2. Covarianza simétrica PSD ─────────────────────────────────────────
TEST(PhaseKalman3, CovarianceStaysSymmetricAndPsd) {
    PhaseKalman3 k;
    k.init(kFs);

    for (int i = 0; i < 20000; ++i) {
        const float z = 0.3f * std::sin(2.0 * M_PI * 220.0 * i / kFs);
        k.tick(z);
        if (i % 100 == 0) {
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) {
                    EXPECT_NEAR(k.P[a][b], k.P[b][a], 1e-3f)
                        << "P asimétrica en i=" << i << " (" << a << "," << b << ")";
                }
            for (int a = 0; a < 3; ++a) {
                EXPECT_TRUE(std::isfinite(k.P[a][a]));
                EXPECT_GE(k.P[a][a], 0.f);
            }
        }
    }
}

// ── 3. Cero NaN/Inf en 1M muestras (con NaN en la entrada) ───────────────
TEST(PhaseKalman3, NoNaNOverOneMillionSamples) {
    PhaseKalman3 k;
    k.init(kFs);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (int i = 0; i < 1'000'000; ++i) {
        float z;
        const int m = i % 997;
        if (m == 0)      z = nan;      // muestra corrupta: debe ignorarse
        else if (m == 1) z = inf;
        else if (m == 2) z = -inf;
        else             z = 0.2f * std::sin(2.0 * M_PI * 1000.0 * i / kFs);
        k.tick(z);

        if ((i & 0x3FFF) == 0) {
            for (int a = 0; a < 3; ++a) {
                EXPECT_TRUE(std::isfinite(k.x[a])) << "x[" << a << "] en i=" << i;
                for (int b = 0; b < 3; ++b)
                    EXPECT_TRUE(std::isfinite(k.P[a][b])) << "P en i=" << i;
            }
            EXPECT_TRUE(std::isfinite(k.predict_next()));
            EXPECT_TRUE(std::isfinite(k.transient_cue()));
            EXPECT_TRUE(std::isfinite(k.gain()));
        }
    }
}

// ── 4. Cue de transitorio: sube en ataques, decae en silencio ──────────
TEST(PhaseKalman3, TransientCueRisesOnAttackAndDecaysOnSilence) {
    PhaseKalman3 k;
    k.init(kFs);

    // 2000 muestras de silencio (cue debe estar ~0)
    for (int i = 0; i < 2000; ++i) k.tick(0.f);
    const float quiet = k.transient_cue();
    EXPECT_LT(quiet, 0.1f);

    // Ataque: escalón de 0 → 0.8 (derivada enorme → cue debe dispararse)
    for (int i = 0; i < 64; ++i) k.tick(0.8f);
    const float attack = k.transient_cue();
    EXPECT_GT(attack, quiet * 5.f)
        << "attack=" << attack << " quiet=" << quiet;

    // Silencio prolongado de nuevo: la cue debe decaer
    for (int i = 0; i < 8000; ++i) k.tick(0.f);
    const float decayed = k.transient_cue();
    EXPECT_LT(decayed, attack)
        << "decayed=" << decayed << " attack=" << attack;
}

// ── 5. Ganancia de Kalman en (0,1) y no colapsada ──────────────────────
TEST(PhaseKalman3, GainStaysBoundedAndDoesNotCollapse) {
    PhaseKalman3 k;
    k.init(kFs);

    for (int i = 0; i < 200000; ++i) {
        const float z = 0.4f * std::sin(2.0 * M_PI * 60.0 * i / kFs);
        k.tick(z);
        const float g = k.gain();
        EXPECT_GT(g, 0.f);
        EXPECT_LT(g, 1.f);
        if (i % 1000 == 0) {
            // El bug previo colapsaba P→0 (K→0) y el filtro se congelaba.
            // Con Q>0 la ganancia debe mantenerse por encima de un piso.
            EXPECT_GT(g, 1e-4f) << "ganancia colapsada en i=" << i;
        }
    }
}

// ── 6. predict_next() = look-ahead 1 consistente ────────────────────────
TEST(PhaseKalman3, PredictNextMatchesConstantVelocityModel) {
    PhaseKalman3 k;
    k.init(kFs);
    k.reset();

    // Estado artificial: pos=1, vel=2, acc=0 → predicción = 1 + 2*dt
    k.x[0] = 1.f; k.x[1] = 2.f; k.x[2] = 0.f;
    const float dt = 1.f / kFs;
    EXPECT_NEAR(k.predict_next(), 1.f + 2.f * dt, 1e-6f);
}

// ── 7. reset() restaura al estado inicial ───────────────────────────────
TEST(PhaseKalman3, ResetRestoresInitialState) {
    PhaseKalman3 k;
    k.init(kFs);
    for (int i = 0; i < 5000; ++i)
        k.tick(0.5f * std::sin(2.0 * M_PI * 330.0 * i / kFs));

    k.reset();
    EXPECT_EQ(k.x[0], 0.f);
    EXPECT_EQ(k.x[1], 0.f);
    EXPECT_EQ(k.x[2], 0.f);
    EXPECT_FLOAT_EQ(k.P[0][0], 1.f);
    EXPECT_FLOAT_EQ(k.P[1][1], 1e4f);
    EXPECT_FLOAT_EQ(k.P[2][2], 10.f);
    EXPECT_NEAR(k.gain(), 1.f / (1.f + k.R), 1e-6f); // contra la R real calibrada (1e-4)
}

}  // namespace
