// test_transducer_operator.cpp — Operador de transducción universal (MANT
// Ec. 1, núcleo usable): compensación por banda con límite de ganancia de
// ruido (WNG). Verifica: corrección exacta con límite holgado, regularización
// visible con límite estricto, no-op sin medida, fail-closed con entradas
// inválidas.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "../spatial/TransducerOperator.hpp"

using ivanna::psi::TransducerDesign;

TEST(TransducerOperator, FlatMeasurementIsNoop) {
    const float measured[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float target[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    TransducerDesign out;
    ASSERT_TRUE(ivanna::psi::designTransducerCompensation(measured, target, 4, 3.0f, out));
    EXPECT_EQ(out.gainsDb.size(), 4u);
    for (float g : out.gainsDb) EXPECT_NEAR(g, 0.0f, 1e-6f);
    EXPECT_NEAR(out.achievedWngDb, 0.0f, 1e-6f);
    EXPECT_NEAR(out.residualRmsDb, 0.0f, 1e-6f);
}

TEST(TransducerOperator, CorrectsBumpExactlyWithLooseLimit) {
    // Medida con +6 dB a 100 Hz y −3 dB a 3 kHz; objetivo plano.
    const float measured[4] = {6.0f, 0.0f, -3.0f, 0.0f};
    const float target[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    TransducerDesign out;
    ASSERT_TRUE(ivanna::psi::designTransducerCompensation(measured, target, 4, 6.0f, out));
    EXPECT_NEAR(out.gainsDb[0], -6.0f, 1e-4f);
    EXPECT_NEAR(out.gainsDb[2], 3.0f, 1e-4f);
    EXPECT_NEAR(out.residualRmsDb, 0.0f, 1e-4f);
}

TEST(TransducerOperator, StrictWngLimitShrinksCorrection) {
    // Un boost grande (+12 dB) con límite WNG +0.5 dB: la corrección debe
    // encogerse (regularización) y el residuo debe ser > 0, pero la WNG
    // resultante debe cumplir el límite.
    const float measured[4] = {-12.0f, -12.0f, -12.0f, -12.0f};
    const float target[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    TransducerDesign loose, strict;
    ASSERT_TRUE(ivanna::psi::designTransducerCompensation(measured, target, 4, 12.0f, loose));
    ASSERT_TRUE(ivanna::psi::designTransducerCompensation(measured, target, 4, 0.5f, strict));
    EXPECT_NEAR(loose.residualRmsDb, 0.0f, 1e-4f);
    EXPECT_GT(strict.residualRmsDb, 0.0f);
    EXPECT_LE(strict.achievedWngDb, 0.5f + 1e-4f);
    EXPECT_LT(strict.gainsDb[0], loose.gainsDb[0]);
}

TEST(TransducerOperator, EqNoiseGainDefinition) {
    const float flat[3] = {0.0f, 0.0f, 0.0f};
    EXPECT_NEAR(ivanna::psi::eqNoiseGainDb(flat, 3), 0.0f, 1e-6f);
    // +3 dB en todas las bandas → WNG +3 dB.
    const float boost[3] = {3.0f, 3.0f, 3.0f};
    EXPECT_NEAR(ivanna::psi::eqNoiseGainDb(boost, 3), 3.0f, 1e-4f);
    // Boost solo en una de cuatro bandas: media lineal (2+1+1+1)/4 = 1.25 → 0.97 dB.
    const float one[4] = {3.0f, 0.0f, 0.0f, 0.0f};
    EXPECT_NEAR(ivanna::psi::eqNoiseGainDb(one, 4), 0.97f, 0.02f);
}

TEST(TransducerOperator, InvalidInputsFailClosed) {
    const float m[2] = {0.0f, 0.0f};
    const float t[2] = {0.0f, 0.0f};
    const float nanArr[2] = {0.0f, std::nanf("")};
    TransducerDesign out;
    EXPECT_FALSE(ivanna::psi::designTransducerCompensation(nullptr, t, 2, 3.0f, out));
    EXPECT_FALSE(ivanna::psi::designTransducerCompensation(m, nullptr, 2, 3.0f, out));
    EXPECT_FALSE(ivanna::psi::designTransducerCompensation(m, t, 0, 3.0f, out));
    EXPECT_FALSE(ivanna::psi::designTransducerCompensation(m, t, 2, std::nanf(""), out));
    EXPECT_FALSE(ivanna::psi::designTransducerCompensation(nanArr, t, 2, 3.0f, out));
    EXPECT_TRUE(out.gainsDb.empty());
}
