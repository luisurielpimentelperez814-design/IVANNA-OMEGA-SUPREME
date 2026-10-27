// Pruebas del gobernador del spread WFS. Verifican los tres defectos que hacían
// que el slider "no tuviera efecto": saturación fuera de apertura, saltos sin
// suavizar y NaN propagado.
#include "spatial/WfsSpreadGovernor.hpp"

#include <gtest/gtest.h>
#include <cmath>

using ivanna::spatial::WfsSpreadGovernor;

TEST(WfsSpreadGovernor, ApertureIsClampedToUsableArrayRadius) {
    WfsSpreadGovernor g;
    g.configure(100.f, 0.12f);
    g.reset(2.0f);
    // spread 2.0 pide 1.5 m por fuente; la apertura útil es 1.35 m.
    for (int i = 0; i < 400; ++i) {
        const auto o = g.process(2.0f, 1.0f);
        EXPECT_LE(o.offsetM, 1.5f * 0.90f + 1e-5f);
    }
    const auto o = g.process(2.0f, 1.0f);
    EXPECT_NEAR(o.offsetM, 1.35f, 1e-4f);
    EXPECT_FALSE(o.clamped);  // 2.0 está dentro del rango admitido
}

TEST(WfsSpreadGovernor, OutOfRangeRequestIsFlaggedAndBounded) {
    WfsSpreadGovernor g;
    g.configure(100.f);
    g.reset(1.f);
    const auto o = g.process(9.0f, 1.f);
    EXPECT_TRUE(o.clamped);
    EXPECT_LE(o.spread, 2.0f + 1e-6f);
}

TEST(WfsSpreadGovernor, NaNDoesNotPropagate) {
    WfsSpreadGovernor g;
    g.configure(100.f);
    g.reset(1.f);
    const auto o = g.process(std::nanf(""), 1.f);
    EXPECT_TRUE(o.sanitized);
    EXPECT_TRUE(std::isfinite(o.spread));
    EXPECT_TRUE(std::isfinite(o.offsetM));
}

TEST(WfsSpreadGovernor, RampsSmoothlyInsteadOfJumping) {
    WfsSpreadGovernor g;
    g.configure(100.f, 0.12f);   // 100 bloques/s => 10 ms por bloque
    g.reset(0.2f);
    float prev = g.spread();
    float maxStep = 0.f;
    for (int i = 0; i < 120; ++i) {
        const auto o = g.process(1.8f, 1.f);
        maxStep = std::max(maxStep, std::fabs(o.spread - prev));
        prev = o.spread;
    }
    // Sin suavizado el salto sería 1.6 en un solo bloque.
    EXPECT_LT(maxStep, 0.20f);
    EXPECT_NEAR(g.spread(), 1.8f, 0.02f);   // converge al objetivo
}

TEST(WfsSpreadGovernor, ConvergesWithinSettlingTime) {
    WfsSpreadGovernor g;
    g.configure(100.f, 0.12f);
    g.reset(0.0f);
    for (int i = 0; i < 60; ++i) g.process(1.0f, 1.f);   // 0,6 s >> 0,12 s
    EXPECT_NEAR(g.spread(), 1.0f, 0.01f);
}

TEST(WfsSpreadGovernor, CoordScaleIsAppliedAndSanitized) {
    WfsSpreadGovernor g;
    g.configure(100.f);
    g.reset(1.f);
    for (int i = 0; i < 200; ++i) g.process(1.f, 0.5f);
    EXPECT_NEAR(g.spread(), 0.5f, 0.02f);
    const auto o = g.process(1.f, std::nanf(""));
    EXPECT_TRUE(std::isfinite(o.spread));
}

TEST(WfsSpreadGovernor, ZeroSpreadCollapsesToCenterWithoutNaN) {
    WfsSpreadGovernor g;
    g.configure(100.f);
    g.reset(0.f);
    for (int i = 0; i < 100; ++i) {
        const auto o = g.process(0.f, 1.f);
        EXPECT_GE(o.offsetM, 0.f);
        EXPECT_TRUE(std::isfinite(o.offsetM));
    }
    EXPECT_NEAR(g.process(0.f, 1.f).offsetM, 0.0f, 1e-6f);
}

TEST(WfsSpreadGovernor, MonotonicInRequestedSpread) {
    WfsSpreadGovernor g;
    g.configure(100.f);
    g.reset(1.f);
    float last = -1.f;
    for (float s = 0.f; s <= 1.2f; s += 0.1f) {
        for (int i = 0; i < 100; ++i) g.process(s, 1.f);
        const float v = g.process(s, 1.f).offsetM;
        EXPECT_GT(v + 1e-6f, last);
        last = v;
    }
}
