#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "../include/ParametricEQ.h"

namespace {

TEST(ParametricEqStressTest, TenSecondsAt48kRapidParameterSweepRemainsFiniteAndStable) {
    constexpr int kSampleRate = 48000;
    constexpr int kTotalFrames = kSampleRate * 10; // 10 s @ 48 kHz
    constexpr int kBlockFrames = 256;

    ivanna::ParametricEQ eq;
    eq.setSampleRate(static_cast<float>(kSampleRate));
    std::vector<float> left(kBlockFrames, 0.0f);
    std::vector<float> right(kBlockFrames, 0.0f);

    int frameCursor = 0;
    int blockIdx = 0;
    while (frameCursor < kTotalFrames) {
        const int frames = std::min(kBlockFrames, kTotalFrames - frameCursor);
        for (int i = 0; i < frames; ++i) {
            const float t = static_cast<float>(frameCursor + i) / static_cast<float>(kSampleRate);
            const float sig = 0.25f * std::sin(2.0f * 3.14159265f * 120.0f * t)
                            + 0.20f * std::sin(2.0f * 3.14159265f * 1000.0f * t)
                            + 0.15f * std::sin(2.0f * 3.14159265f * 6500.0f * t);
            left[i]  = sig;
            right[i] = sig * 0.95f;
        }

        // Modular continuamente los parámetros del EQ cada bloque (prueba anti-inestabilidad IIR)
        ivanna::DSPParams p{};
        p.sampleRate = static_cast<uint32_t>(kSampleRate);
        const float mod = std::sin(0.07f * static_cast<float>(blockIdx));
        p.low      = 6.0f * mod;
        p.mid      = -4.0f * std::cos(0.05f * static_cast<float>(blockIdx));
        p.high     = 5.0f * std::sin(0.11f * static_cast<float>(blockIdx));
        p.presence = 3.0f * mod;
        eq.setParams(p);

        eq.process(left.data(), right.data(), frames);

        for (int i = 0; i < frames; ++i) {
            ASSERT_TRUE(std::isfinite(left[i])) << "NaN/Inf en L[" << i << "] bloque " << blockIdx;
            ASSERT_TRUE(std::isfinite(right[i])) << "NaN/Inf en R[" << i << "] bloque " << blockIdx;
            ASSERT_LE(std::fabs(left[i]), 8.0f) << "Inestabilidad de biquad L en bloque " << blockIdx;
            ASSERT_LE(std::fabs(right[i]), 8.0f) << "Inestabilidad de biquad R en bloque " << blockIdx;
        }

        frameCursor += frames;
        ++blockIdx;
    }
}

} // namespace
