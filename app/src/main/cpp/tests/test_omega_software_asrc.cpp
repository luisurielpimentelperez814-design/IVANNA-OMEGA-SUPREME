#include <cmath>
#include <vector>
#include <limits>
#include "gtest/gtest.h"
#include "omega_software_asrc.hpp"

using ivanna::audio::OmegaSoftwareAsrc;

TEST(OmegaSoftwareAsrc, RejectsInvalidRatesAndAcceptsStandardAudioRates) {
    OmegaSoftwareAsrc asrc;
    EXPECT_FALSE(asrc.configure(0, 48000));
    EXPECT_FALSE(asrc.configured());
    EXPECT_TRUE(asrc.configure(44100, 48000));
    EXPECT_TRUE(asrc.configured());
}

TEST(OmegaSoftwareAsrc, BoundedPpmCorrectionFailsClosedOnNan) {
    OmegaSoftwareAsrc asrc;
    ASSERT_TRUE(asrc.configure(48000, 48000));
    asrc.setClockCorrectionPpm(1.0e9);
    EXPECT_DOUBLE_EQ(asrc.clockCorrectionPpm(), 500.0);
    asrc.setClockCorrectionPpm(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(asrc.clockCorrectionPpm(), 0.0);
}

TEST(OmegaSoftwareAsrc, ConvertsStreamingStereoWithoutNonFiniteSamples) {
    OmegaSoftwareAsrc asrc;
    ASSERT_TRUE(asrc.configure(44100, 48000));
    constexpr std::size_t frames = 2048;
    std::vector<float> input(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = 0.2f * std::sin(2.0 * 3.141592653589793 * 1000.0 * i / 44100.0);
        input[2 * i] = s;
        input[2 * i + 1] = -s;
    }
    std::vector<float> output(5000 * 2);
    const auto count = asrc.process(input.data(), frames, output.data(), 5000);
    EXPECT_GT(count, 0u);
    EXPECT_NEAR(static_cast<double>(count), frames * 48000.0 / 44100.0, 5.0);
    for (std::size_t i = 0; i < count * 2; ++i) EXPECT_TRUE(std::isfinite(output[i]));
    for (std::size_t i = 0; i < count; ++i)
        EXPECT_NEAR(output[2 * i], -output[2 * i + 1], 1.0e-5);
}

TEST(OmegaSoftwareAsrc, TooSmallOutputBufferDoesNotConsumeInput) {
    OmegaSoftwareAsrc asrc;
    ASSERT_TRUE(asrc.configure(44100, 96000));
    std::vector<float> input(512 * 2, 0.25f);
    std::vector<float> small(2);
    EXPECT_EQ(asrc.process(input.data(), 512, small.data(), 1), 0u);
    std::vector<float> output(2048 * 2);
    EXPECT_GT(asrc.process(input.data(), 512, output.data(), 2048), 0u);
}
