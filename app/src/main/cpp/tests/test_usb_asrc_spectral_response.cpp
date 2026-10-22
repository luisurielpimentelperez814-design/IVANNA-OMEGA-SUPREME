#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>

#include "usb_asrc_kernel.hpp"

namespace {
constexpr int kTaps = 64;
constexpr int kPhases = 1024;
constexpr double kPi = 3.14159265358979323846;

double responseMagnitude(const float (&coeff)[kPhases][kTaps],
                         double cyclesPerSample) {
    // Phase zero is symmetric around (Taps - 1) / 2; evaluate its frequency response.
    constexpr double center = (kTaps - 1) * 0.5;
    double real = 0.0;
    double imag = 0.0;
    for (int k = 0; k < kTaps; ++k) {
        const double angle = 2.0 * kPi * cyclesPerSample * (k - center);
        real += coeff[0][k] * std::cos(angle);
        imag -= coeff[0][k] * std::sin(angle);
    }
    return std::sqrt(real * real + imag * imag);
}
}

TEST(UsbAsrcSpectralResponse, DownsamplingRejectsEnergyAboveOutputNyquist) {
    static float kernel[kPhases][kTaps];
    ivanna::audio::buildUsbAsrcKernel(48000, 24000, kernel);
    const double passband = responseMagnitude(kernel, 6000.0 / 48000.0);
    const double stopband = responseMagnitude(kernel, 18000.0 / 48000.0);
    EXPECT_GT(passband, 0.97) << "6 kHz passband response=" << passband;
    EXPECT_LT(stopband, 0.02) << "18 kHz alias source response=" << stopband;
}

TEST(UsbAsrcSpectralResponse, PreservesHighAudioBandWhenDownsamplingByTwo) {
    static float kernel[kPhases][kTaps];
    ivanna::audio::buildUsbAsrcKernel(96000, 48000, kernel);
    const double passband = responseMagnitude(kernel, 18000.0 / 96000.0);
    const double stopband = responseMagnitude(kernel, 36000.0 / 96000.0);
    EXPECT_GT(passband, 0.95) << "18 kHz passband response=" << passband;
    EXPECT_LT(stopband, 0.02) << "36 kHz alias source response=" << stopband;
}

TEST(UsbAsrcSpectralResponse, InvalidRatesFailClosedToFiniteSilence) {
    static float kernel[kPhases][kTaps];
    for (int i = 0; i < kPhases; ++i)
        for (int j = 0; j < kTaps; ++j)
            kernel[i][j] = 123.0f;

    ivanna::audio::buildUsbAsrcKernel(0, 48000, kernel);
    for (int i = 0; i < kPhases; ++i)
        for (int j = 0; j < kTaps; ++j) {
            ASSERT_TRUE(std::isfinite(kernel[i][j]));
            EXPECT_FLOAT_EQ(kernel[i][j], 0.0f);
        }

    ivanna::audio::buildUsbAsrcKernel(48000, -1, kernel);
    for (int i = 0; i < kPhases; ++i)
        for (int j = 0; j < kTaps; ++j)
            EXPECT_FLOAT_EQ(kernel[i][j], 0.0f);
}

TEST(UsbAsrcSpectralResponse, EveryPhaseHasFiniteCoefficientsAndUnityDcGain) {
    static float kernel[kPhases][kTaps];
    ivanna::audio::buildUsbAsrcKernel(44100, 96000, kernel);
    for (int phase = 0; phase < kPhases; ++phase) {
        double sum = 0.0;
        for (int tap = 0; tap < kTaps; ++tap) {
            ASSERT_TRUE(std::isfinite(kernel[phase][tap])) << "phase=" << phase << " tap=" << tap;
            sum += kernel[phase][tap];
        }
        EXPECT_NEAR(sum, 1.0, 2.0e-5) << "phase=" << phase;
    }
}
