// test_rt_gain_slew.cpp — Limitadores de pendiente de Ruta B (v2.5.386).
// Cubre: slewLimitValue (paso acotado, NaN fail-closed), capCompressorRatio
// (tope 4:1), y la simulación de bombeo: un objetivo que oscila 1.0->0.5->1.0
// por bloque NO debe mover la ganancia aplicada más de 0.02 por bloque.
#include <gtest/gtest.h>
#include "rt_gain_slew.hpp"

using ivanna::rt::slewLimitValue;
using ivanna::rt::capCompressorRatio;
using ivanna::rt::kAdaptiveGainSlewPerBlock;
using ivanna::rt::kCompressorThresholdSlewDbPerBlock;
using ivanna::rt::kCompressorRatioSlewPerBlock;

TEST(RtGainSlew, MovesTowardTargetByAtMostMaxStep) {
    EXPECT_FLOAT_EQ(slewLimitValue(1.0f, 0.5f, 0.02f), 0.98f);
    EXPECT_FLOAT_EQ(slewLimitValue(0.5f, 1.0f, 0.02f), 0.52f);
    // Dentro del paso: llega al objetivo exacto.
    EXPECT_FLOAT_EQ(slewLimitValue(0.99f, 1.0f, 0.02f), 1.0f);
    EXPECT_FLOAT_EQ(slewLimitValue(1.0f, 1.0f, 0.02f), 1.0f);
}

TEST(RtGainSlew, NanTargetKeepsCurrent) {
    EXPECT_FLOAT_EQ(slewLimitValue(0.8f, NAN, 0.02f), 0.8f);
    EXPECT_FLOAT_EQ(slewLimitValue(NAN, 0.8f, 0.02f), 0.8f);
}

TEST(RtGainSlew, OscillatingTargetCannotPump) {
    // Simula el bug de la 384/385: el objetivo adaptativo oscila 1.0 -> 0.5
    // -> 1.0 en bloques consecutivos. La ganancia aplicada debe moverse como
    // mucho 0.02 por bloque (sin diente de sierra audible).
    float applied = 1.0f;
    float maxJump = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float target = (i % 2 == 0) ? 0.5f : 1.0f;
        const float next = slewLimitValue(applied, target, kAdaptiveGainSlewPerBlock);
        maxJump = std::max(maxJump, std::fabs(next - applied));
        applied = next;
    }
    EXPECT_LE(maxJump, kAdaptiveGainSlewPerBlock + 1e-6f);
    // Y tras 200 bloques de objetivo 0.5 fijo, converge al objetivo.
    for (int i = 0; i < 100; ++i) applied = slewLimitValue(applied, 0.5f, kAdaptiveGainSlewPerBlock);
    EXPECT_FLOAT_EQ(applied, 0.5f);
}

TEST(RtGainSlew, CompressorRatioCappedAtFourToOne) {
    EXPECT_FLOAT_EQ(capCompressorRatio(20.0f), 4.0f);
    EXPECT_FLOAT_EQ(capCompressorRatio(8.0f), 4.0f);
    EXPECT_FLOAT_EQ(capCompressorRatio(2.54f), 2.54f);
    EXPECT_FLOAT_EQ(capCompressorRatio(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(capCompressorRatio(0.5f), 1.0f);
    EXPECT_FLOAT_EQ(capCompressorRatio(NAN), 1.0f);
}

TEST(RtGainSlew, ThresholdAndRatioSlewStepsAreBounded) {
    EXPECT_FLOAT_EQ(slewLimitValue(-5.5f, -30.0f, kCompressorThresholdSlewDbPerBlock), -6.5f);
    EXPECT_FLOAT_EQ(slewLimitValue(1.0f, 4.0f, kCompressorRatioSlewPerBlock), 1.25f);
}
