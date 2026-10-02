// test_woodworth_itd.cpp — Prueba T5: Woodworth Esférico ITD + Sombra de Cabeza Brown-Duda (M8)
#include <gtest/gtest.h>
#include "../spatial/ObjectSpatialRenderer.hpp"
#include <cmath>
#include <vector>

namespace {

using namespace ivanna::spatial;

TEST(WoodworthItdTest, ZeroAngleAndPiOverTwoFollowExactSphericalFormula) {
    constexpr float kSr = 48000.0f;

    // (a) theta = 0 (x = 0, y = 1.5) -> ITD = 0
    const float itd0 = ObjectSpatialRenderer::computeWoodworthItdSamples(0.0f, 1.5f, kSr, 1.0f);
    EXPECT_NEAR(itd0, 0.0f, 1.0e-5f);

    // (b) theta = pi/2 (x = 1.0, y = 0.0) -> ITD = (a/c)*(pi/2 + 1)*fs ≈ 31.48 muestras @ 48 kHz (± 1 muestra)
    const float itd90 = ObjectSpatialRenderer::computeWoodworthItdSamples(1.0f, 0.0f, kSr, 1.0f);
    const float expected90 = (0.0875f / 343.0f) * (1.57079632679f + 1.0f) * kSr;
    EXPECT_NEAR(itd90, expected90, 0.1f);
    EXPECT_NEAR(itd90, 31.5f, 1.0f);
}

TEST(WoodworthItdTest, HeadShadowAttenuates4kHzContralateralEarBeyondIld) {
    constexpr float kSr = 48000.0f;
    constexpr size_t N  = 4096;

    ObjectSpatialRenderer renderer;
    renderer.prepare(kSr);

    std::vector<float> sig(N), zero(N, 0.0f), outL(N, 0.0f), outR(N, 0.0f);
    for (size_t i = 0; i < N; ++i) {
        sig[i] = 0.5f * std::sin(6.2831853f * 4000.0f * static_cast<float>(i) / kSr);
    }

    // Colocar un único objeto a x = +4.0, y = 0.5 (theta ≈ 1.45 rad ≈ pi/2 hacia la derecha)
    std::array<DecomposedObject, 4> objs{};
    objs[0].position = {4.0f, 0.5f, 0.0f};
    objs[0].gain     = 1.0f;
    for (int k = 1; k < 4; ++k) objs[k].gain = 0.0f;

    const float* ptrs[4] = {sig.data(), zero.data(), zero.data(), zero.data()};
    renderer.renderObjects(ptrs, objs, outL.data(), outR.data(), N, 1.0f, 1.0f);

    double eL = 0.0, eR = 0.0;
    for (size_t i = N / 2; i < N; ++i) {
        eL += static_cast<double>(outL[i]) * outL[i];
        eR += static_cast<double>(outR[i]) * outR[i];
    }
    const float rmsL = static_cast<float>(std::sqrt(eL / (N / 2)));
    const float rmsR = static_cast<float>(std::sqrt(eR / (N / 2)));

    // Calcular la razón puramente por ILD (sin sombra de cabeza)
    float ildL = 0.0f, ildR = 0.0f;
    ObjectSpatialRenderer::computeIldGains(4.0f, 0.5f, ildL, ildR);
    const float ildDiffDb = 20.0f * std::log10((ildR + 1e-12f) / (ildL + 1e-12f));
    const float totalDiffDb = 20.0f * std::log10((rmsR + 1e-12f) / (rmsL + 1e-12f));
    const float headShadowExtraDb = totalDiffDb - ildDiffDb;

    EXPECT_GE(headShadowExtraDb, 3.0f)
        << "Sombra de cabeza a 4 kHz debe aportar >= 3 dB adicionales sobre ILD (medido: "
        << headShadowExtraDb << " dB)";
}

} // namespace
