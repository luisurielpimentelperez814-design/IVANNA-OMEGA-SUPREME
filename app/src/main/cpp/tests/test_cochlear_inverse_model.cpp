// test_cochlear_inverse_model.cpp — suite host del Eje Supremo Neuroacústico.
#include <gtest/gtest.h>
#include "neuromorphic/CochlearActiveInverseModel.hpp"
#include <cmath>
#include <vector>
#include <cstdlib>

using ivanna::neuromorphic::CochlearActiveInverseEngine;

TEST(CochlearInverse, ZeroLatencyAndImpulseResponse) {
    CochlearActiveInverseEngine eng;
    eng.prepare(48000.f);
    std::vector<float> L(256, 0.f), R(256, 0.f);
    L[0] = 1.0f; R[0] = 1.0f;
    eng.process(L.data(), R.data(), 256);
    // 0.00 ms de latencia algorítmica: la muestra 0 responde al impulso YA.
    EXPECT_TRUE(std::isfinite(L[0]));
    EXPECT_NE(L[0], 0.0f);
    // y el resto del bloque es finito
    for (float v : L) { EXPECT_FALSE(std::isnan(v)); EXPECT_FALSE(std::isinf(v)); }
}

TEST(CochlearInverse, NumericalStabilityNoNaN) {
    CochlearActiveInverseEngine eng;
    eng.prepare(48000.f);
    eng.setIntensity(1.0f);   // peor caso: inversión a tope
    std::vector<float> L(512), R(512);
    std::srand(1234);
    for (int blk = 0; blk < 50; ++blk) {
        for (int i = 0; i < 512; ++i) {
            // estocástica + subnormales + picos extremos
            float r = (static_cast<float>(std::rand()) / RAND_MAX) * 2.f - 1.f;
            L[i] = r * ((i % 3 == 0) ? 1e-25f : 1.0f);
            R[i] = r * ((i % 5 == 0) ? 1e-30f : 0.8f);
        }
        eng.process(L.data(), R.data(), 512);
        for (int i = 0; i < 512; ++i) {
            EXPECT_FALSE(std::isnan(L[i]));
            EXPECT_FALSE(std::isinf(L[i]));
            EXPECT_FALSE(std::isnan(R[i]));
            EXPECT_FALSE(std::isinf(R[i]));
        }
    }
}

TEST(CochlearInverse, HarmonicLinearizationEnergy) {
    CochlearActiveInverseEngine eng;
    eng.prepare(48000.f);
    eng.setIntensity(0.35f);
    std::vector<float> L(2048), R(2048);
    double ein = 0.0;
    for (int i = 0; i < 2048; ++i) {
        const float t = static_cast<float>(i) / 48000.f;
        L[i] = 0.35f * std::sin(2.f * 3.14159265f * 220.f * t)
             + 0.30f * std::sin(2.f * 3.14159265f * 2770.f * t)
             + 0.20f * std::sin(2.f * 3.14159265f * 9000.f * t);
        R[i] = L[i];
        ein += double(L[i]) * L[i];
    }
    eng.process(L.data(), R.data(), 2048);
    double eout = 0.0, peak = 0.0;
    for (int i = 64; i < 2048; ++i) {           // saltar el settling del biquad
        eout += double(L[i]) * L[i];
        peak = std::max(peak, std::fabs((double)L[i]));
        EXPECT_TRUE(std::isfinite(L[i]));
    }
    // Transparente y acotada: sin clipping descontrolado, energía comparable.
    EXPECT_LT(peak, 2.5);
    EXPECT_GT(eout, ein * 0.05);   // no anula la señal
    EXPECT_LT(eout, ein * 20.0);   // no explota
}

TEST(CochlearInverse, LockFreeControlAndBypass) {
    CochlearActiveInverseEngine eng;
    eng.prepare(48000.f);

    EXPECT_TRUE(eng.isEnabled());
    EXPECT_NEAR(eng.intensity(), 0.35f, 1e-4f);
    EXPECT_TRUE(eng.isActive());

    // Control de intensidad
    eng.setIntensity(0.75f);
    EXPECT_NEAR(eng.intensity(), 0.75f, 1e-4f);

    // Bypass bit-exacto cuando disabled
    eng.setEnabled(false);
    EXPECT_FALSE(eng.isEnabled());
    EXPECT_FALSE(eng.isActive());

    std::vector<float> L(128, 0.42f), R(128, -0.42f);
    eng.process(L.data(), R.data(), 128);
    for (int i = 0; i < 128; ++i) {
        EXPECT_EQ(L[i], 0.42f);
        EXPECT_EQ(R[i], -0.42f);
    }

    // Reactivación lock-free
    eng.setEnabled(true);
    EXPECT_TRUE(eng.isEnabled());
    EXPECT_TRUE(eng.isActive());
}

