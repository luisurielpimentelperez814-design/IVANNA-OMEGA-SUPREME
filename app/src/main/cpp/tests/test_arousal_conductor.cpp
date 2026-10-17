// ============================================================================
//  test_arousal_conductor.cpp — barrera real de immersion/ArousalConductor.hpp
//
//  Qué se verifica (sin smoke tests):
//   1. Silencio: excitación ~0, fase Calm, objetivos mínimos.
//   2. Subida gradual: pasa por Build, anticipa (>0) y termina en Peak.
//   3. Release lento: tras cortar, la curva NO cae de golpe (>60 % a 1 s) y
//      sí se apaga a ~12 s.
//   4. Patrón de drop: hueco de energía sube la tensión; el retorno dispara
//      UN solo tick de pulso de impacto.
//   5. NaN/Inf no envenenan el estado; todo queda en [0,1].
//   6. Ningún objetivo se mueve más rápido que su límite de velocidad.
//   7. Los techos se respetan.
// ============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "../immersion/ArousalConductor.hpp"

using namespace ivanna::immersion;

namespace {

constexpr float kRate = 20.0f;
constexpr int kTicksPerSec = 20;

ArousalFeatures drive(float d) {
    // Reparte "d" por los cuatro rasgos de forma coherente con la mezcla interna.
    ArousalFeatures f;
    f.onsetRate = 8.0f * d;
    f.lowEnergy = d;
    f.brightness = d;
    f.level = d * d;  // sqrt(level) == d
    return f;
}

ArousalConductor makeConductor() {
    ArousalConductor c;
    c.prepare(kRate);
    return c;
}

}  // namespace

TEST(ArousalConductor, SilenceStaysCalm) {
    auto c = makeConductor();
    ImmersionTargets t;
    for (int i = 0; i < 400; ++i) t = c.update(drive(0.0f));
    EXPECT_LT(t.arousal, 1e-3f);
    EXPECT_EQ(t.phase, ArousalPhase::Calm);
    EXPECT_LT(t.hapticGain, 0.01f);
    EXPECT_LT(t.fieldOpenness, 0.01f);
    EXPECT_NEAR(t.harmonicGain, 0.40f, 0.02f);
}

TEST(ArousalConductor, BuildIsAnticipatedAndEndsInPeak) {
    auto c = makeConductor();
    bool sawBuild = false;
    float maxAnticipation = 0.0f, prevArousal = 0.0f;
    ImmersionTargets t;
    // Rampa de 0 a 0.9 en 10 s
    for (int i = 0; i < 10 * kTicksPerSec; ++i) {
        const float d = 0.9f * static_cast<float>(i) / (10.0f * kTicksPerSec);
        t = c.update(drive(d));
        if (t.phase == ArousalPhase::Build) sawBuild = true;
        maxAnticipation = std::max(maxAnticipation, t.anticipation);
        EXPECT_GE(t.arousal + 1e-6f, prevArousal) << "la excitación no debe bajar en una subida";
        prevArousal = t.arousal;
    }
    EXPECT_TRUE(sawBuild);
    EXPECT_GT(maxAnticipation, 0.2f);
    // Sostener arriba
    for (int i = 0; i < 12 * kTicksPerSec; ++i) t = c.update(drive(0.9f));
    EXPECT_EQ(t.phase, ArousalPhase::Peak);
    EXPECT_GT(t.fieldOpenness, 0.7f);
    EXPECT_GT(t.hapticGain, 0.5f);
}

TEST(ArousalConductor, ReleaseIsSlow) {
    auto c = makeConductor();
    ImmersionTargets t;
    for (int i = 0; i < 20 * kTicksPerSec; ++i) t = c.update(drive(0.9f));
    const float peak = t.arousal;
    for (int i = 0; i < kTicksPerSec; ++i) t = c.update(drive(0.0f));
    EXPECT_GT(t.arousal, 0.6f * peak);   // 1 s después sigue alta
    for (int i = 0; i < 11 * kTicksPerSec; ++i) t = c.update(drive(0.0f));
    EXPECT_LT(t.arousal, 0.2f * peak);   // 12 s después casi apagada
}

TEST(ArousalConductor, DropPatternRaisesTensionThenSingleImpact) {
    auto c = makeConductor();
    ImmersionTargets t;
    for (int i = 0; i < 10 * kTicksPerSec; ++i) t = c.update(drive(0.9f));
    float maxTension = 0.0f;
    for (int i = 0; i < static_cast<int>(0.6f * kTicksPerSec); ++i) {
        t = c.update(drive(0.02f));
        maxTension = std::max(maxTension, t.tension);
    }
    EXPECT_GT(maxTension, 0.6f);
    int impacts = 0;
    float hapticAtImpact = 0.0f;
    for (int i = 0; i < 3 * kTicksPerSec; ++i) {
        t = c.update(drive(0.95f));
        if (t.impactPulse > 0.5f) {
            ++impacts;
            hapticAtImpact = t.hapticGain;
        }
    }
    EXPECT_EQ(impacts, 1) << "el pulso debe durar exactamente un tick";
    EXPECT_GT(hapticAtImpact, 0.0f);
}

TEST(ArousalConductor, NonFiniteInputDoesNotPoison) {
    auto c = makeConductor();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    ArousalFeatures bad{nan, inf, -inf, nan};
    ImmersionTargets t;
    for (int i = 0; i < 200; ++i) t = c.update(bad);
    EXPECT_TRUE(std::isfinite(t.arousal));
    for (int i = 0; i < 200; ++i) t = c.update(drive(0.8f));
    EXPECT_GT(t.arousal, 0.3f);
    for (float v : {t.arousal, t.anticipation, t.tension, t.impactPulse, t.fieldOpenness,
                    t.hapticGain, t.harmonicGain}) {
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 1.0f);
    }
}

TEST(ArousalConductor, SlewLimitsAreNeverExceeded) {
    auto c = makeConductor();
    uint32_t s = 12345u;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; };
    const float dt = 1.0f / kRate;
    ImmersionTargets prev = c.last();
    for (int i = 0; i < 4000; ++i) {
        // Cambios abruptos a propósito: la mitad del tiempo, 0 o 1.
        const float d = (rnd() < 0.5f) ? (rnd() < 0.5f ? 0.0f : 1.0f) : rnd();
        const ImmersionTargets t = c.update(drive(d));
        EXPECT_LE(t.fieldOpenness - prev.fieldOpenness, ArousalConductor::kOpenUpPerSec * dt + 1e-5f);
        EXPECT_LE(prev.fieldOpenness - t.fieldOpenness, ArousalConductor::kOpenDownPerSec * dt + 1e-5f);
        EXPECT_LE(t.hapticGain - prev.hapticGain, ArousalConductor::kHapticUpPerSec * dt + 1e-5f);
        EXPECT_LE(prev.hapticGain - t.hapticGain, ArousalConductor::kHapticDownPerSec * dt + 1e-5f);
        EXPECT_LE(t.harmonicGain - prev.harmonicGain, ArousalConductor::kHarmUpPerSec * dt + 1e-5f);
        EXPECT_LE(prev.harmonicGain - t.harmonicGain, ArousalConductor::kHarmDownPerSec * dt + 1e-5f);
        prev = t;
    }
}

TEST(ArousalConductor, CeilingsAreRespected) {
    auto c = makeConductor();
    c.setCeilings(0.3f, 0.2f, 0.5f);
    ImmersionTargets t;
    for (int i = 0; i < 30 * kTicksPerSec; ++i) t = c.update(drive(1.0f));
    EXPECT_LE(t.fieldOpenness, 0.3f + 1e-5f);
    EXPECT_LE(t.hapticGain, 0.2f + 1e-5f);
    EXPECT_LE(t.harmonicGain, 0.5f + 1e-5f);
}
