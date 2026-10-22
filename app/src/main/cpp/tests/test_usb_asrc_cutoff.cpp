#include <gtest/gtest.h>
#include <cmath>
#include "usb_asrc_cutoff.hpp"

TEST(UsbAsrcCutoff, UnityRateKeepsTransitionBelowNyquist) {
    const double cutoff = ivanna::audio::usbAsrcCutoffNyquistNormalized(48000, 48000);
    EXPECT_NEAR(cutoff, 0.94, 1e-12);
    EXPECT_LT(cutoff, 1.0);
}

TEST(UsbAsrcCutoff, DownsamplingTracksOutputNyquist) {
    const double cutoff = ivanna::audio::usbAsrcCutoffNyquistNormalized(96000, 48000);
    EXPECT_NEAR(cutoff, 0.47, 1e-12);
    EXPECT_LT(cutoff, 0.5);
}

TEST(UsbAsrcCutoff, UpsamplingDoesNotRaiseCutoffAboveInputNyquist) {
    const double cutoff = ivanna::audio::usbAsrcCutoffNyquistNormalized(48000, 96000);
    EXPECT_NEAR(cutoff, 0.94, 1e-12);
    EXPECT_LT(cutoff, 1.0);
}

TEST(UsbAsrcCutoff, InvalidRatesFailClosed) {
    EXPECT_DOUBLE_EQ(ivanna::audio::usbAsrcCutoffNyquistNormalized(0, 48000), 0.0);
    EXPECT_DOUBLE_EQ(ivanna::audio::usbAsrcCutoffNyquistNormalized(48000, -1), 0.0);
}
