#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "usb_asrc_cutoff.hpp"
#include "usb_asrc_kernel.hpp"

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

TEST(UsbAsrcPhase, RoundsNormalFractionToExpectedPolyphaseIndex) {
    int base = -1;
    int phaseIndex = -1;
    const auto disposition = ivanna::audio::normalizeUsbAsrcPhase(
        12.25, 64, 1024, base, phaseIndex);
    EXPECT_EQ(disposition, ivanna::audio::UsbAsrcPhaseDisposition::Ready);
    EXPECT_EQ(base, 12);
    EXPECT_EQ(phaseIndex, 256);
}

TEST(UsbAsrcPhase, DefersRoundedPhaseThatWouldReadPastBlockEnd) {
    int base = -1;
    int phaseIndex = -1;
    const auto disposition = ivanna::audio::normalizeUsbAsrcPhase(
        63.9999, 64, 1024, base, phaseIndex);
    EXPECT_EQ(disposition, ivanna::audio::UsbAsrcPhaseDisposition::DeferToNextBlock);
    EXPECT_EQ(base, 64);
    EXPECT_EQ(phaseIndex, 0);
}

TEST(UsbAsrcPhase, DefersExtremeFinitePhaseBeforeIntegerConversion) {
    int base = -1;
    int phaseIndex = -1;
    const auto disposition = ivanna::audio::normalizeUsbAsrcPhase(
        std::numeric_limits<double>::max(), 64, 1024, base, phaseIndex);
    EXPECT_EQ(disposition, ivanna::audio::UsbAsrcPhaseDisposition::DeferToNextBlock);
    EXPECT_EQ(base, 64);
    EXPECT_EQ(phaseIndex, 0);
}

TEST(UsbAsrcPhase, RejectsInvalidPhaseWithoutProducingAnIndex) {
    int base = -1;
    int phaseIndex = -1;
    const auto disposition = ivanna::audio::normalizeUsbAsrcPhase(
        std::numeric_limits<double>::quiet_NaN(), 64, 1024, base, phaseIndex);
    EXPECT_EQ(disposition, ivanna::audio::UsbAsrcPhaseDisposition::Invalid);
}
