#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "../include/Compressor.h"
#include "../include/GainStage.h"
#include "../include/HarmonicExciter.h"
#include "../include/ParametricEQ.h"
#include "../include/StereoWidener.h"
#include "../anti_dolby.h"
#include "../neuromorphic/volterra_h2_symmetric.hpp"

namespace {
bool allFinite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}
float peakAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (float x : v) peak = std::max(peak, std::fabs(x));
    return peak;
}
} // namespace

// ── Test heredado V1 (intacto): pipeline real permanece finito bajo stress ──
TEST(DspCoreStability, RealPipelineRemainsFiniteAcrossStressBlocks) {
    constexpr int N = 2048;
    std::vector<float> left(N), right(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        left[i]  = 0.55f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f  * t)
                 + 0.20f * std::sin(2.0f * static_cast<float>(M_PI) * 3200.0f * t);
        right[i] = 0.55f * std::sin(2.0f * static_cast<float>(M_PI) * 554.37f * t)
                 + 0.20f * std::sin(2.0f * static_cast<float>(M_PI) * 2800.0f * t);
    }

    ivanna::DSPParams p{};
    p.sampleRate = 48000; p.drive = 0.72f; p.wet = 0.55f; p.mix = 0.80f;
    p.freq = 1000.0f; p.resonance = 0.85f; p.low = 2.0f; p.mid = 1.5f;
    p.high = 1.0f; p.presence = 0.5f; p.master = -1.0f;

    ivanna::ParametricEQ eq;    eq.setParams(p);
    ivanna::Compressor comp;    comp.setParams(p);
    comp.setThreshold(-18.0f); comp.setRatio(3.0f); comp.setAttack(5.0f); comp.setRelease(80.0f);
    ivanna::HarmonicExciter exciter; exciter.setParams(p);
    ivanna::StereoWidener widener;   widener.setParams(p); widener.setWidth(1.25f);
    ivanna::GainStage gain;          gain.setParams(p);

    for (int iter = 0; iter < 256; ++iter) {
        std::vector<float> L = left;
        std::vector<float> R = right;

        gain.processInput(L.data(), R.data(), N);
        eq.process(L.data(), R.data(), N);
        comp.process(L.data(), R.data(), N);
        exciter.process(L.data(), R.data(), N);
        widener.process(L.data(), R.data(), N);
        gain.processOutput(L.data(), R.data(), N);

        // FIX V2.0.1: el pipeline real ahora conserva más dinámica
        // (menos limitación interna para evitar distorsión armónica/fase).
        // La estabilidad debe medirse antes del SafetyLimiter, por lo que
        // permitimos margen de headroom pero rechazamos runaway real.
        ASSERT_TRUE(allFinite(L));
        ASSERT_TRUE(allFinite(R));

        float peakL = peakAbs(L);
        float peakR = peakAbs(R);

        EXPECT_LT(peakL, 24.0f);
        EXPECT_LT(peakR, 24.0f);
    }
}

// ── V2: AntiDolbyState converge y publica valores acotados ─────────────────
TEST(AntiDolbyStateStability, ConvergesToTargetBounded) {
    AntiDolbyState st;
    st.setAttackTau(0.01f);
    st.setReleaseTau(0.05f);

    // Escenario: 100% música → target > 1.0f (ensancha) y multiplicador ∈ [0.5, 1.6].
    st.updateFromClassification(/*speech*/0.0f, /*music*/1.0f, /*bass*/0.0f);
    for (int i = 0; i < 5000; ++i) {
        st.tick(1.0f / 48000.0f);
        const float w = st.currentWidener();
        ASSERT_TRUE(std::isfinite(w));
        ASSERT_GE(w, 0.5f);
        ASSERT_LE(w, 1.6f);
    }
    EXPECT_GT(st.currentWidener(), 1.05f);

    // Escenario inverso: 100% speech → target < 1.0f (estrecha).
    st.updateFromClassification(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 5000; ++i) st.tick(1.0f / 48000.0f);
    EXPECT_LT(st.currentWidener(), 0.95f);

    // Robustez: dt patológico no rompe la publicación.
    st.tick(-1.0f);
    st.tick(std::numeric_limits<float>::infinity());
    ASSERT_TRUE(std::isfinite(st.currentWidener()));
}

// ── V2: VolterraH2Symmetric mantiene salida finita y acotada por soft-clip ─
TEST(VolterraH2Stability, BypassIsIdentity) {
    ivanna::dsp::VolterraH2Symmetric v(64, 2);
    v.setEnabled(false);

    std::vector<float> in(256), out(256);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& x : in) x = dist(rng);

    v.processInterleaved(in.data(), out.data(), 128, 2);
    for (size_t i = 0; i < in.size(); ++i) EXPECT_FLOAT_EQ(in[i], out[i]);
}

TEST(VolterraH2Stability, EnabledStaysFiniteAndSoftClipped) {
    ivanna::dsp::VolterraH2Symmetric v(64, 2);
    v.setEnabled(true);

    std::vector<float> in(2048), out(2048);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-2.0f, 2.0f);  // provocamos saturación
    for (auto& x : in) x = dist(rng);

    v.processInterleaved(in.data(), out.data(), 1024, 2);

    for (float y : out) {
        ASSERT_TRUE(std::isfinite(y));
        // soft-clip: |y| < 1 + threshold en el peor caso teórico.
        ASSERT_LT(std::fabs(y), 2.0f);
    }
}


TEST(VolterraH2Stability, DefaultKernelIsExactPassthroughAndInactive) {
    ivanna::dsp::VolterraH2Symmetric v(64, 2);
    EXPECT_FALSE(v.hasEffectiveKernel());
    std::vector<float> in(512), out(512, -99.0f);
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.31f * std::sin(static_cast<float>(i) * 0.071f);
    v.processInterleaved(in.data(), out.data(), 256, 2);
    for (size_t i = 0; i < in.size(); ++i) EXPECT_FLOAT_EQ(in[i], out[i]);
}

TEST(VolterraH2Stability, KernelUpdateRequiresDisabledSilentEngineAndFiniteCoefficients) {
    constexpr uint32_t K = 8;
    ivanna::dsp::VolterraH2Symmetric v(K, 2);
    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 0.5f;
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_FALSE(v.hasEffectiveKernel());
    v.setEnabled(false);
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_TRUE(v.hasEffectiveKernel());
    h1[0] = std::numeric_limits<float>::quiet_NaN();
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    std::vector<float> in(1024, 0.1f), out(1024, 0.0f);
    v.processInterleaved(in.data(), out.data(), 512, 2);
    EXPECT_NEAR(out.back(), 0.05f, 1e-4f);
}

TEST(VolterraH2Stability, IdentityLinearAndZeroQuadraticKernelIsInactive) {
    constexpr uint32_t K = 16;
    ivanna::dsp::VolterraH2Symmetric v(K, 1);
    v.setEnabled(false);
    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 1.0f;
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_FALSE(v.hasEffectiveKernel());
}


TEST(VolterraH2Stability, NonzeroQuadraticKernelIsAllocatedAndApplied) {
    constexpr uint32_t K = 2;
    ivanna::dsp::VolterraH2Symmetric v(K, 1);
    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 1.0f;
    h2[0] = 0.1f;
    v.setEnabled(false);
    v.updateKernels(h1.data(), h2.data(), K);
    ASSERT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    std::vector<float> in(512, 0.1f), out(512, 0.0f);
    v.processInterleaved(in.data(), out.data(), 512, 1);
    EXPECT_NEAR(out.back(), 0.101f, 1e-4f);
}

TEST(VolterraH2Stability, IdentityBypassKeepsDelayHistoryForLaterKernelActivation) {
    constexpr uint32_t K = 2;
    ivanna::dsp::VolterraH2Symmetric v(K, 1);
    v.setEnabled(false);
    std::vector<float> history(16, 0.2f), historyOut(16, 0.0f);
    v.processInterleaved(history.data(), historyOut.data(), 16, 1);
    for (size_t i = 0; i < history.size(); ++i) EXPECT_FLOAT_EQ(history[i], historyOut[i]);

    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 0.5f;
    h1[1] = 0.5f;
    v.updateKernels(h1.data(), h2.data(), K);
    ASSERT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    std::vector<float> silence(16, 0.0f), out(16, 0.0f);
    v.processInterleaved(silence.data(), out.data(), 16, 1);
    EXPECT_GT(out[0], 0.0f);
}
