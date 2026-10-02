// test_ime_style_blender.cpp — Prueba T3: Inferencia Bayesiana 12D, Histéresis y Compuerta Smoothstep
#include <gtest/gtest.h>
#include "../music_intelligence/StyleBlender.hpp"
#include <cmath>

namespace {

using namespace ivanna::ime;

TEST(ImeStyleBlenderTest, AllTwelveCentroidsYieldArgmaxAndHighConfidence) {
    int count = 0;
    const StyleProto* lib = defaultAtlasLibrary(count);
    ASSERT_EQ(count, 12);

    for (int k = 0; k < count; ++k) {
        StyleBlender blender;
        blender.setLibrary(lib, count);
        StyleDecision dec{};
        // Alimentar 4 ventanas de 2.0 s con f = mu_k exacto
        for (int step = 0; step < 4; ++step) {
            dec = blender.update(lib[k].mu, 2.0f);
        }
        EXPECT_EQ(dec.current, k) << "Centroide k=" << k << " (" << lib[k].name << ") no ganó argmax";
        EXPECT_GT(dec.conf, 0.60f) << "Confianza insuficiente en centroide k=" << k;
        EXPECT_GT(dec.gate, 0.95f) << "Gate smoothstep no abrió en centroide k=" << k;
    }
}

TEST(ImeStyleBlenderTest, ThreeStepHysteresisPreventsSingleStepFlicker) {
    int count = 0;
    const StyleProto* lib = defaultAtlasLibrary(count);
    StyleBlender blender;
    blender.setLibrary(lib, count);

    // Converger a estilo 0 (progressive_rock_70s)
    for (int i = 0; i < 4; ++i) {
        (void)blender.update(lib[0].mu, 2.0f);
    }
    ASSERT_EQ(blender.currentStyleIndex(), 0);

    // 1 solo paso con f = mu_5 (electronic_dense) NO debe conmutar current (kHold = 3)
    auto d1 = blender.update(lib[5].mu, 2.0f);
    EXPECT_EQ(d1.current, 0) << "Histéresis rota: conmutó en 1 solo paso";

    // Paso 2 con f = mu_5 todavía retiene current == 0
    auto d2 = blender.update(lib[5].mu, 2.0f);
    EXPECT_EQ(d2.current, 0) << "Histéresis rota: conmutó en 2 pasos";

    // Paso 3 consecutivo con f = mu_5 conmuta a 5
    auto d3 = blender.update(lib[5].mu, 2.0f);
    EXPECT_EQ(d3.current, 5) << "No conmutó tras 3 pasos consecutivos";
}

TEST(ImeStyleBlenderTest, LowConfidenceEquidistantPointGatesToExactNeutral) {
    // Dos prototipos simétricos para crear un punto exactamente equidistante (ps = [0.5, 0.5] => conf = 0)
    const StyleProto symLib[2] = {
        {"style_a", {0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f},
                    {10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f},
                    {0.9f, 0.8f, 2.0f, 0.4f, 0.7f, 0.9f}},
        {"style_b", {0.8f,0.8f,0.8f,0.8f,0.8f,0.8f,0.8f,0.8f,0.8f,0.8f,0.8f,0.8f},
                    {10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f,10.f},
                    {0.1f, 0.2f, -2.0f, 0.9f, 0.3f, 0.1f}}
    };
    StyleBlender blender;
    blender.setLibrary(symLib, 2);

    const float mid[12] = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
    const auto dec = blender.update(mid, 2.0f);
    const SceneTargets neutral{};

    EXPECT_LT(dec.conf, 0.35f);
    EXPECT_FLOAT_EQ(dec.gate, 0.0f);
    EXPECT_FLOAT_EQ(dec.t.wfsSpread,      neutral.wfsSpread);
    EXPECT_FLOAT_EQ(dec.t.hrtfDepth,      neutral.hrtfDepth);
    EXPECT_FLOAT_EQ(dec.t.eqTiltDb,       neutral.eqTiltDb);
    EXPECT_FLOAT_EQ(dec.t.dynamicsAmount, neutral.dynamicsAmount);
    EXPECT_FLOAT_EQ(dec.t.envDepth,       neutral.envDepth);
    EXPECT_FLOAT_EQ(dec.t.warmth,         neutral.warmth);
}

TEST(ImeStyleBlenderTest, SlewRateBoundedPerDecisionStep) {
    int count = 0;
    const StyleProto* lib = defaultAtlasLibrary(count);
    StyleBlender blender;
    blender.setLibrary(lib, count);

    auto prev = blender.update(lib[1].mu, 4.0f).t;
    for (int step = 0; step < 6; ++step) {
        const auto cur = blender.update(lib[11].mu, 2.0f).t;
        EXPECT_LE(std::fabs(cur.wfsSpread - prev.wfsSpread), 0.1001f);
        EXPECT_LE(std::fabs(cur.hrtfDepth - prev.hrtfDepth), 0.1001f);
        EXPECT_LE(std::fabs(cur.envDepth  - prev.envDepth),  0.1001f);
        EXPECT_LE(std::fabs(cur.warmth    - prev.warmth),    0.1001f);
        prev = cur;
    }
}

} // namespace
