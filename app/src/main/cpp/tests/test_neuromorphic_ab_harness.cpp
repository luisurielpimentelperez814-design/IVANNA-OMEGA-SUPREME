/**
 * test_neuromorphic_ab_harness.cpp — Banco A/B del aporte audible (prueba de NULL)
 *
 * El banco existía sin ningún test que lo ejerciera: una herramienta de medición sin medir.
 * Aquí se le exige lo que promete, sobre señales con respuesta conocida:
 *   1. B == A            -> identidad exacta, residuo nulo, nada audible.
 *   2. cambio de ganancia -> coherencia ~1 y gainDb correcto (no se confunde con coloración).
 *   3. retardo puro       -> se detecta y se alinea: el residuo tras alinear es nulo.
 *   4. residuo bajo el umbral por banda -> INAUDIBLE; por encima -> audible en SU banda.
 *   5. entradas inválidas y coste de host.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/NeuromorphicAbHarness.hpp"

namespace {
constexpr float kSr = 48000.0f;
constexpr int   kN  = 48000;

// Señal suave (ruido pasa-bajos ~380 Hz, LCG determinista): ancho de correlación de ~20
// muestras. El banco busca el retardo con paso grueso y afina ±16, así que necesita señal
// con correlación más ancha que su paso (música real lo cumple; ruido blanco puro NO).
std::vector<float> smoothSignal(int n) {
    std::vector<float> x(n);
    uint32_t s = 12345u;
    float lp = 0.0f;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        const float w = ((float)(s >> 8) / 8388608.0f) - 1.0f;
        lp += 0.05f * (w - lp);
        x[i] = 4.0f * lp;
    }
    return x;
}

// Un tono por banda del banco (50 Hz, 200 Hz, 1 kHz, 5 kHz, 12 kHz).
std::vector<float> fiveBandTones(int n) {
    const float f[5] = {50.0f, 200.0f, 1000.0f, 5000.0f, 12000.0f};
    std::vector<float> x(n, 0.0f);
    for (int i = 0; i < n; ++i)
        for (float fk : f)
            x[i] += 0.1f * std::sin(2.0f * 3.14159265f * fk * (float)i / kSr);
    return x;
}
} // namespace

TEST(NeuromorphicAbHarness, IdenticalSignalsAreExactNull) {
    ivanna::measure::AbHarness h; h.prepare(kSr);
    const auto a = fiveBandTones(kN);
    const auto r = h.measure(a.data(), a.data(), kN);
    EXPECT_TRUE(r.identical);
    EXPECT_FALSE(r.audibleContribution);
    EXPECT_EQ(r.audibleBands, 0);
    EXPECT_LE(r.residualRmsDb, -150.0f);
}

TEST(NeuromorphicAbHarness, PureGainChangeIsNotMistakenForColoration) {
    ivanna::measure::AbHarness h; h.prepare(kSr);
    const auto a = smoothSignal(kN);
    std::vector<float> b(a);
    const float g = std::pow(10.0f, 3.0f / 20.0f);   // +3 dB
    for (auto& v : b) v *= g;
    const auto r = h.measure(a.data(), b.data(), kN);
    EXPECT_FALSE(r.identical);
    EXPECT_GT(r.coherence, 0.999f);                   // misma forma de onda
    EXPECT_NEAR(r.gainDb, 3.0f, 0.15f);
    EXPECT_NEAR(r.delaySamples, 0.0f, 0.5f);
}

TEST(NeuromorphicAbHarness, PureDelayIsDetectedAndAligned) {
    ivanna::measure::AbHarness h; h.prepare(kSr);
    const auto a = smoothSignal(kN);
    const int D = 37;
    std::vector<float> b(kN, 0.0f);
    for (int i = D; i < kN; ++i) b[i] = a[i - D];     // B retrasada D muestras
    const auto r = h.measure(a.data(), b.data(), kN);
    EXPECT_NEAR(r.delaySamples, (float)D, 0.5f);
    EXPECT_GT(r.coherence, 0.999f);
    // Sin alinear el residuo sería enorme; alineado, la resta mide el efecto (aquí: ninguno).
    EXPECT_LE(r.residualRmsDb, -100.0f);
    EXPECT_EQ(r.audibleBands, 0);
}

TEST(NeuromorphicAbHarness, ResidualBelowThresholdIsInaudibleAboveIsAudibleInItsBand) {
    ivanna::measure::AbHarness h; h.prepare(kSr);
    const auto a = fiveBandTones(kN);
    auto withAir = [&](float dbRel) {
        std::vector<float> b(a);
        const float amp = 0.1f * std::pow(10.0f, dbRel / 20.0f);
        for (int i = 0; i < kN; ++i)
            b[i] += amp * std::sin(2.0f * 3.14159265f * 12000.0f * (float)i / kSr);
        return b;
    };
    const auto quiet = withAir(-70.0f);
    const auto loud  = withAir(-20.0f);
    const auto rq = h.measure(a.data(), quiet.data(), kN);
    const auto rl = h.measure(a.data(), loud.data(),  kN);

    EXPECT_EQ(rq.audibleBands, 0);
    EXPECT_FALSE(rq.audibleContribution);             // -70 dB: sugestión, no aporte

    // El reparto por bandas usa filtros de un polo (6 dB/oct): un residuo de 12 kHz se fuga a
    // las bandas vecinas (definición/presencia), así que solo se exige lo que el banco
    // garantiza: su banda propia ("aire", 8-20 kHz) es audible y las bandas LEJANAS (sub,
    // cuerpo) permanecen limpias. La fuga a vecinas es una limitación conocida del banco.
    EXPECT_TRUE(rl.bands[4].audible);
    EXPECT_FALSE(rl.bands[0].audible);
    EXPECT_FALSE(rl.bands[1].audible);
    EXPECT_TRUE(rl.audibleContribution);
    EXPECT_GT(rl.bands[4].deltaDb, ivanna::measure::AbHarness::kAudibilityThresholdDb);
}

TEST(NeuromorphicAbHarness, InvalidInputsAndHostCost) {
    ivanna::measure::AbHarness h; h.prepare(kSr);
    const auto a = smoothSignal(64);
    const auto r0 = h.measure(nullptr, a.data(), 64);
    const auto r1 = h.measure(a.data(), a.data(), 0);
    EXPECT_FALSE(r0.audibleContribution);
    EXPECT_FALSE(r1.audibleContribution);
    EXPECT_EQ(h.hostCostRatio(500000000ull, 0), 0.0f);
    // 0.5 s de host para 1 s de audio -> 0.5 (cabe en tiempo real).
    EXPECT_NEAR(h.hostCostRatio(500000000ull, (int)kSr), 0.5f, 1.0e-4f);
}
