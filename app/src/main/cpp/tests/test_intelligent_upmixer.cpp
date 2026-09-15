#include <gtest/gtest.h>
#include "../spatial/IntelligentUpmixer.hpp"
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace Ivanna;

// ── Invariantes originales (siguen vigentes, no se relajan) ──────────────────

TEST(IntelligentUpmixerTest, IdenticalStereoYieldsFrontalHOA) {
    IntelligentUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setUpmixingEnabled(true);
    upmixer.setImmersivity(1.0f);

    std::vector<float> inL(256, 0.5f);
    std::vector<float> inR(256, 0.5f);
    std::vector<HoaVector> outField;

    upmixer.processBlock(inL.data(), inR.data(), outField, 256);

    ASSERT_EQ(outField.size(), 256u);
    for (size_t i = 100; i < 256; ++i) {   // saltar el asentamiento del filtro
        EXPECT_NEAR(outField[i][1], 0.0f, 1e-4f);  // Y (lateral) ≈ 0
        EXPECT_GT(outField[i][0], 0.0f);           // W (omnidireccional)
        EXPECT_GT(outField[i][3], 0.0f);           // X (frontal)
    }
}

TEST(IntelligentUpmixerTest, TransparentModeBypass) {
    IntelligentUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setUpmixingEnabled(false);

    std::vector<float> inL(256, 0.5f);
    std::vector<float> inR(256, 0.0f);
    std::vector<HoaVector> outField;

    upmixer.processBlock(inL.data(), inR.data(), outField, 256);

    HoaVector encL = HoaGainMatrix::encode(M_PI / 6.0f);
    for (size_t i = 0; i < 256; ++i) {
        EXPECT_NEAR(outField[i][0], encL[0] * 0.5f, 1e-5f);
        EXPECT_NEAR(outField[i][1], encL[1] * 0.5f, 1e-5f);
        EXPECT_NEAR(outField[i][3], encL[3] * 0.5f, 1e-5f);
    }
}

TEST(IntelligentUpmixerTest, IndependentNoiseEnergySpread) {
    IntelligentUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setUpmixingEnabled(true);
    upmixer.setImmersivity(1.0f);

    std::vector<float> inL(256), inR(256);
    for (size_t i = 0; i < 256; ++i) {
        inL[i] = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
        inR[i] = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
    }

    std::vector<HoaVector> outField;
    upmixer.processBlock(inL.data(), inR.data(), outField, 256);

    float sumY2 = 0.0f, sumX2 = 0.0f;
    for (size_t i = 0; i < 256; ++i) {
        sumY2 += outField[i][1] * outField[i][1];
        sumX2 += outField[i][3] * outField[i][3];
    }
    EXPECT_GT(sumY2, 1.0f);
    EXPECT_GT(sumX2, 1.0f);
}

// ── Refinamiento magistral: invariantes nuevos y más fuertes ────────────────

namespace {
float lateralEnergy(const std::vector<HoaVector>& f, size_t from, size_t to) {
    float s = 0.0f;
    for (size_t i = from; i < to; ++i) s += f[i][1] * f[i][1];
    return s;
}
float omniEnergy(const std::vector<HoaVector>& f, size_t from, size_t to) {
    float s = 0.0f;
    for (size_t i = from; i < to; ++i) s += f[i][0] * f[i][0];
    return s;
}
std::vector<float> uncorrelatedNoise(size_t n, unsigned seed) {
    std::srand(seed);
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
    return v;
}
} // namespace

// La inmersividad DEBE ensanchar el campo lateral de forma monótona
// (mismo material, sólo cambia el control) — y no tocar la energía ómnidireccional.
TEST(IntelligentUpmixerTest, ImmersivityExpandsLateralFieldMonotonically) {
    constexpr size_t N = 8192;             // >> 15 ms: el suavizado converge
    auto l = uncorrelatedNoise(N, 12345);
    auto r = uncorrelatedNoise(N, 54321);

    auto run = [&](float imm) {
        IntelligentUpmixer u;
        u.prepare(48000.0f);
        u.setUpmixingEnabled(true);
        u.setImmersivity(imm);
        std::vector<HoaVector> f;
        u.processBlock(l.data(), r.data(), f, N);
        return std::make_pair(lateralEnergy(f, N - 2048, N), omniEnergy(f, N - 2048, N));
    };

    auto low  = run(0.15f);
    auto high = run(1.00f);

    EXPECT_GT(high.first, low.first * 2.0f);           // expansión lateral clara
    // La energía omnidireccional (W) no se dispara con la inmersividad: es un
    // control de ANCHURA, no de nivel.
    EXPECT_LT(high.second, low.second * 4.0f);
}

// El crossover es mono-seguro: graves idénticos L/R quedan al centro, sin fuga lateral.
TEST(IntelligentUpmixerTest, LowFrequencyCorrelatedStaysOmnidirectional) {
    constexpr size_t N = 8192;
    std::vector<float> l(N), r(N);
    for (size_t i = 0; i < N; ++i) {
        const float s = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 100.0f * i / 48000.0f);
        l[i] = s; r[i] = s;
    }
    IntelligentUpmixer u;
    u.prepare(48000.0f);
    u.setUpmixingEnabled(true);
    u.setImmersivity(1.0f);
    std::vector<HoaVector> f;
    u.processBlock(l.data(), r.data(), f, N);

    const float lat = lateralEnergy(f, N - 2048, N);
    const float omni = omniEnergy(f, N - 2048, N);
    EXPECT_LT(lat, omni * 1e-3f);   // prácticamente cero lateral
    EXPECT_GT(omni, 1.0f);
}

// Entradas no finitas jamás se propagan a la salida (robustez en el hilo de audio).
TEST(IntelligentUpmixerTest, NonFiniteInputNeverPropagatesNaN) {
    constexpr size_t N = 512;
    std::vector<float> l(N, 0.25f), r(N, 0.25f);
    l[10] = std::nanf(""); l[20] = INFINITY;
    r[30] = -INFINITY;     r[40] = std::nanf("");
    IntelligentUpmixer u;
    u.prepare(48000.0f);
    u.setUpmixingEnabled(true);
    u.setImmersivity(1.0f);
    std::vector<HoaVector> f;
    u.processBlock(l.data(), r.data(), f, N);
    for (size_t i = 0; i < N; ++i)
        for (int ch = 0; ch < kHoaNumChannels; ++ch)
            EXPECT_TRUE(std::isfinite(f[i][ch])) << "NaN/Inf en frame " << i << " canal " << ch;
}

// El buffer de salida se dimensiona exacto al número de frames (sin desbordes).
TEST(IntelligentUpmixerTest, OutputSizeMatchesFrameCount) {
    IntelligentUpmixer u;
    u.prepare(48000.0f);
    u.setImmersivity(1.0f);
    std::vector<float> l(1000, 0.1f), r(1000, -0.1f);
    std::vector<HoaVector> f;
    u.processBlock(l.data(), r.data(), f, 1000);
    EXPECT_EQ(f.size(), 1000u);
    std::vector<HoaVector> g;
    u.processBlock(l.data(), r.data(), g, 128);
    EXPECT_EQ(g.size(), 128u);
}
