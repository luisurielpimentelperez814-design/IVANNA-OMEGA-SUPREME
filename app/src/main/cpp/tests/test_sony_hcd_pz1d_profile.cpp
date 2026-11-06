#include <gtest/gtest.h>
#include "../neuromorphic/sony_hcd_pz1d_profile.hpp"

using namespace ivanna::dsp::sony_hcd_pz1d;

TEST(SonyHcdPz1dProfile, StoresReportedRailAndTimingData) {
    EXPECT_FLOAT_EQ(kHighRailPositiveV, 58.2f);
    EXPECT_FLOAT_EQ(kHighRailNoLoadMagnitudeV, 62.1f);
    EXPECT_FLOAT_EQ(kHighRailFullPowerMagnitudeV, 52.4f);
    EXPECT_FLOAT_EQ(kLowRailPositiveV, 31.5f);
    EXPECT_FLOAT_EQ(kClassHSwitchThresholdPeakV, 24.5f);
    EXPECT_FLOAT_EQ(kClassHGuardBandV, 7.0f);
    EXPECT_EQ(kClassHTurnOnNs, 850u);
    EXPECT_FLOAT_EQ(kClassHReleaseMinMs, 4.5f);
    EXPECT_FLOAT_EQ(kClassHReleaseMaxMs, 12.0f);
}

TEST(SonyHcdPz1dProfile, RcCornerMatchesReportedNetwork) {
    EXPECT_NEAR(kDcDetectCutoffHz, 0.072f, 0.001f);
}

TEST(SonyHcdPz1dProfile, ClassHStateSwitchesAndRetainsHighRail) {
    ClassHState state;
    EXPECT_FALSE(state.update(24.4f, 48000u));
    EXPECT_TRUE(state.update(24.5f, 48000u));
    for (int i = 0; i < 575; ++i) EXPECT_TRUE(state.update(0.0f, 48000u));
    EXPECT_FALSE(state.update(0.0f, 48000u));
}

TEST(SonyHcdPz1dProfile, HarmonicModelSelectsTransitionH3) {
    const auto low = simulateMeasuredHarmonics(0.5f, false);
    const auto high = simulateMeasuredHarmonics(0.5f, true);
    EXPECT_TRUE(std::isfinite(low.sample));
    EXPECT_TRUE(std::isfinite(high.sample));
    EXPECT_GT(high.sample, low.sample);
    EXPECT_TRUE(high.class_h_high_rail);
}
