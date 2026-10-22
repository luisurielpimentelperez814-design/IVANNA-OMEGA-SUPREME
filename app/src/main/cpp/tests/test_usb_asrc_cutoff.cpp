#include <gtest/gtest.h>
#include <cmath>
#include "usb_asrc_cutoff.hpp"

TEST(UsbAsrcCutoff, UnityRateStaysBelowNyquist) {
    const double cutoff = ivanna::audio::usbAsrcCutoffCyclesPerSample(48000, 48000);
    EXPECT_NEAR(cutoff, 0.47, 1e-12);
    EXPECT_LT(cutoff, 0.5);
}

TEST(UsbAsrcCutoff, DownsamplingTracksOutputNyquist) {
    const double cutoff = ivanna::audio::usbAsrcCutoffCyclesPerSample(96000, 48000);
    EXPECT_NEAR(cutoff, 0.235, 1e-12);
    EXPECT_LT(cutoff, 0.25);
}

TEST(UsbAsrcCutoff, UpsamplingDoesNotRaiseCutoffAboveInputNyquist) {
    const double cutoff = ivanna::audio::usbAsrcCutoffCyclesPerSample(48000, 96000);
    EXPECT_NEAR(cutoff, 0.47, 1e-12);
    EXPECT_LT(cutoff, 0.5);
}

TEST(UsbAsrcCutoff, NormalizedSincArgumentUsesTwiceCyclesPerSample) {
    EXPECT_NEAR(ivanna::audio::usbAsrcCutoffNormalized(48000, 48000), 0.94, 1e-12);
    EXPECT_LT(ivanna::audio::usbAsrcCutoffNormalized(48000, 48000), 1.0);
    EXPECT_NEAR(ivanna::audio::usbAsrcCutoffNormalized(96000, 48000), 0.47, 1e-12);
    EXPECT_LT(ivanna::audio::usbAsrcCutoffNormalized(96000, 48000), 0.5);
    EXPECT_DOUBLE_EQ(ivanna::audio::usbAsrcCutoffNormalized(0, 48000), 0.0);
}

TEST(UsbAsrcCutoff, InvalidRatesFailClosed) {
    EXPECT_DOUBLE_EQ(ivanna::audio::usbAsrcCutoffCyclesPerSample(0, 48000), 0.0);
    EXPECT_DOUBLE_EQ(ivanna::audio::usbAsrcCutoffCyclesPerSample(48000, -1), 0.0);
}
