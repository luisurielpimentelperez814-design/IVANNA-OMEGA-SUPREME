#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "include/g_pll_core.hpp"

using ivanna::gfe::GeometricPll;

TEST(GeometricPll, RejectsInvalidConfigurationAndSamples) {
    GeometricPll pll(48000.0);
    EXPECT_FALSE(pll.configure(0.0, 10.0, 1.0, 100.0));
    EXPECT_FALSE(pll.update(0.0, 0.001).valid);
    ASSERT_TRUE(pll.configure(48000.0, 10.0, 1.0, 100.0));
    EXPECT_FALSE(pll.update(std::numeric_limits<double>::quiet_NaN(), 0.001).valid);
    EXPECT_FALSE(pll.update(0.0, 0.0).valid);
}

TEST(GeometricPll, InitializesToWrappedReferencePhase) {
    GeometricPll pll(48000.0);
    const auto s = pll.update(4.0, 1.0 / 48000.0);
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.phaseRadians, std::remainder(4.0, 2.0 * 3.14159265358979323846), 1e-12);
}

TEST(GeometricPll, TracksSmallFrequencyOffsetWithoutUnboundedCorrection) {
    GeometricPll pll(1000.0, 10.0, 1.0, 20.0);
    constexpr double dt = 0.001;
    double reference = 0.0;
    ASSERT_TRUE(pll.update(reference, dt).valid);
    ivanna::gfe::PllStatus state;
    for (int i = 0; i < 5000; ++i) {
        reference = std::remainder(reference + 2.0 * 3.14159265358979323846 * 1001.0 * dt,
                                   2.0 * 3.14159265358979323846);
        state = pll.update(reference, dt);
        ASSERT_TRUE(state.valid);
        EXPECT_LE(std::abs(state.frequencyRadiansPerSecond / (2.0 * 3.14159265358979323846) - 1000.0), 20.1);
    }
    EXPECT_LT(std::abs(state.phaseErrorRadians), 0.2);
}

TEST(GeometricPll, ZeroDriftRemainsBoundedForFiveSeconds) {
    constexpr double pi = 3.14159265358979323846;
    constexpr double dt = 0.001;
    GeometricPll pll(1000.0, 3.0, 1.0, 0.05);
    double reference = 0.0;
    ASSERT_TRUE(pll.update(reference, dt).valid);
    ivanna::gfe::PllStatus state;
    for (int i = 0; i < 5000; ++i) {
        reference = std::remainder(reference + 2.0 * pi * 1000.0 * dt, 2.0 * pi);
        state = pll.update(reference, dt);
        ASSERT_TRUE(state.valid);
        EXPECT_LE(std::abs(state.frequencyRadiansPerSecond / (2.0 * pi) - 1000.0), 0.051);
        EXPECT_TRUE(std::isfinite(state.phaseRadians));
        EXPECT_TRUE(std::isfinite(state.phaseErrorRadians));
    }
    EXPECT_LT(std::abs(state.phaseErrorRadians), 0.05);
}

TEST(GeometricPll, CorrectionRespectsConfiguredFrequencyLimit) {
    constexpr double pi = 3.14159265358979323846;
    constexpr double dt = 0.001;
    constexpr double maxPullHz = 0.05;
    GeometricPll pll(1000.0, 3.0, 1.0, maxPullHz);
    double reference = 0.0;
    ASSERT_TRUE(pll.update(reference, dt).valid);
    ivanna::gfe::PllStatus state;
    for (int i = 0; i < 1000; ++i) {
        reference = std::remainder(reference + 2.0 * pi * 1001.0 * dt, 2.0 * pi);
        state = pll.update(reference, dt);
        ASSERT_TRUE(state.valid);
        EXPECT_LE(std::abs(state.frequencyRadiansPerSecond / (2.0 * pi) - 1000.0),
                  maxPullHz + 1e-9);
    }
}
