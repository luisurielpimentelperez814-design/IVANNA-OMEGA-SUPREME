#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include "dsp/AdaptiveGainRamp.hpp"

TEST(AdaptiveGainRamp, DisableReturnsToUnityMonotonicallyWithoutOvershoot) {
    std::array<float, 8> left{}, right{};
    left.fill(1.0f);
    right.fill(0.5f);

    const float next = ivanna::dsp::applyStereoGainRamp(
        left.data(), right.data(), left.size(), 0.60f, 1.0f);

    EXPECT_FLOAT_EQ(next, 1.0f);
    for (std::size_t i = 0; i < left.size(); ++i) {
        EXPECT_GE(left[i], 0.60f);
        EXPECT_LE(left[i], 1.0f);
        EXPECT_GE(right[i], 0.30f);
        EXPECT_LE(right[i], 0.50f);
        if (i > 0) {
            EXPECT_GE(left[i], left[i - 1]);
            EXPECT_GE(right[i], right[i - 1]);
        }
    }
    EXPECT_FLOAT_EQ(left.back(), 1.0f);
    EXPECT_FLOAT_EQ(right.back(), 0.5f);
}

TEST(AdaptiveGainRamp, EnableOnlyAttenuatesAndIsMonotonic) {
    std::array<float, 8> left{}, right{};
    left.fill(1.0f);
    right.fill(-1.0f);

    const float next = ivanna::dsp::applyStereoGainRamp(
        left.data(), right.data(), left.size(), 1.0f, 0.5f);

    EXPECT_FLOAT_EQ(next, 0.5f);
    for (std::size_t i = 0; i < left.size(); ++i) {
        EXPECT_LE(left[i], 1.0f);
        EXPECT_GE(left[i], 0.5f);
        EXPECT_GE(right[i], -1.0f);
        EXPECT_LE(right[i], -0.5f);
        if (i > 0) {
            EXPECT_LE(left[i], left[i - 1]);
            EXPECT_LE(right[i], right[i - 1]);
        }
    }
    EXPECT_FLOAT_EQ(left.back(), 0.5f);
    EXPECT_FLOAT_EQ(right.back(), -0.5f);
}

TEST(AdaptiveGainRamp, ClampsUnsafeGainsAndDefaultsNonFiniteValuesToUnity) {
    std::array<float, 2> left{1.0f, 1.0f}, right{1.0f, 1.0f};
    const float next = ivanna::dsp::applyStereoGainRamp(
        left.data(), right.data(), left.size(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(next, 1.0f);
    EXPECT_FLOAT_EQ(left[0], 1.0f);
    EXPECT_FLOAT_EQ(left[1], 1.0f);
    EXPECT_FLOAT_EQ(right[0], 1.0f);
    EXPECT_FLOAT_EQ(right[1], 1.0f);

    const float bounded = ivanna::dsp::applyStereoGainRamp(
        left.data(), right.data(), left.size(), 2.0f, -1.0f);
    EXPECT_FLOAT_EQ(bounded, 0.5f);
    EXPECT_NEAR(left.back(), 0.5f, 1e-6f);
}

TEST(AdaptiveGainRamp, EmptyOrNullBuffersAreSafe) {
    EXPECT_FLOAT_EQ(ivanna::dsp::applyStereoGainRamp(nullptr, nullptr, 8, 0.7f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ivanna::dsp::applyStereoGainRamp(nullptr, nullptr, 0, 0.7f, 0.8f), 0.8f);
}
