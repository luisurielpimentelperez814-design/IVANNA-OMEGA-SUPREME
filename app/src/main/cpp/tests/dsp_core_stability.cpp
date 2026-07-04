#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "../include/Compressor.h"
#include "../include/GainStage.h"
#include "../include/HarmonicExciter.h"
#include "../include/ParametricEQ.h"
#include "../include/StereoWidener.h"

namespace {
bool allFinite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

float peakAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (float x : v) peak = std::max(peak, std::fabs(x));
    return peak;
}
}

TEST(DspCoreStability, RealPipelineRemainsFiniteAcrossStressBlocks) {
    constexpr int N = 2048;
    std::vector<float> left(N), right(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        left[i] = 0.55f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t)
                + 0.20f * std::sin(2.0f * static_cast<float>(M_PI) * 3200.0f * t);
        right[i] = 0.55f * std::sin(2.0f * static_cast<float>(M_PI) * 554.37f * t)
                 + 0.20f * std::sin(2.0f * static_cast<float>(M_PI) * 2800.0f * t);
    }

    ivanna::DSPParams p{};
    p.sampleRate = 48000;
    p.drive = 0.72f;
    p.wet = 0.55f;
    p.mix = 0.80f;
    p.freq = 1000.0f;
    p.resonance = 0.85f;
    p.low = 2.0f;
    p.mid = 1.5f;
    p.high = 1.0f;
    p.presence = 0.5f;
    p.master = -1.0f;

    ivanna::ParametricEQ eq;
    eq.setParams(p);

    ivanna::Compressor comp;
    comp.setParams(p);
    comp.setThreshold(-18.0f);
    comp.setRatio(3.0f);
    comp.setAttack(5.0f);
    comp.setRelease(80.0f);

    ivanna::HarmonicExciter exciter;
    exciter.setParams(p);
    exciter.setAmount(0.45f);

    ivanna::StereoWidener widener;
    widener.setParams(p);
    widener.setWidth(1.25f);

    ivanna::GainStage gain;
    gain.setParams(p);

    for (int iter = 0; iter < 256; ++iter) {
        std::vector<float> L = left;
        std::vector<float> R = right;

        gain.processInput(L.data(), R.data(), N);
        eq.process(L.data(), R.data(), N);
        comp.process(L.data(), R.data(), N);
        exciter.process(L.data(), R.data(), N);
        widener.process(L.data(), R.data(), N);
        gain.processOutput(L.data(), R.data(), N);

        ASSERT_TRUE(allFinite(L));
        ASSERT_TRUE(allFinite(R));
        EXPECT_LT(peakAbs(L), 8.0f);
        EXPECT_LT(peakAbs(R), 8.0f);
    }
}
