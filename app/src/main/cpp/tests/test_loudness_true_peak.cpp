#include <gtest/gtest.h>
#include "../dsp/loudness_meter.hpp"
#include <algorithm>
#include <cmath>

TEST(LoudnessTruePeak, TruePeakNeverBelowSamplePeakForSameSignal) {
    ivanna::metering::LoudnessMeter meter;
    constexpr int n = 48000;
    float peak = 0.0f;
    float samples[2 * 256];
    int filled = 0;
    for (int i = 0; i < n; ++i) {
        // Non-bin-centered, high-frequency tone exposes inter-sample peaks.
        const float x = 0.91f * std::sin(2.0 * 3.14159265358979323846 * 0.237 * i + 0.173);
        peak = std::max(peak, std::fabs(x));
        samples[filled++] = x;
        samples[filled++] = -0.83f * x;
        if (filled == static_cast<int>(sizeof(samples) / sizeof(samples[0]))) {
            meter.feedInterleavedStereo(samples, filled / 2);
            filled = 0;
        }
    }
    if (filled > 0) meter.feedInterleavedStereo(samples, filled / 2);
    EXPECT_GE(meter.truePeakDbtp(), meter.peakDbfs() - 1e-4f);
    EXPECT_NEAR(meter.peakDbfs(), 20.0f * std::log10(peak), 0.05f);
}

TEST(LoudnessTruePeak, SignedInterpolationPreservesInterSampleOvershoot) {
    const float signedSamples[4] = {0.8887f, -0.5659f, -0.9584f, 0.8372f};
    const float magnitudeOnly[4] = {0.8887f, 0.5659f, 0.9584f, 0.8372f};
    const float signedPeak = ivanna::metering::LoudnessMeter::interpolatePeak4x(signedSamples);
    const float magnitudePeak = ivanna::metering::LoudnessMeter::interpolatePeak4x(magnitudeOnly);
    EXPECT_GT(signedPeak, 0.9584f);
    EXPECT_GT(std::fabs(signedPeak - magnitudePeak), 1e-3f);
}
