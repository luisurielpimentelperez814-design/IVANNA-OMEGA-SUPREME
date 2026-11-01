#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "../include/fotoholo_bark_meter.hpp"

namespace {
std::vector<float> tone(float hz, float sampleRate, std::size_t frames, float amplitude = 0.5f) {
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i)
        out[i] = amplitude * std::sin(6.2831853071795864769f * hz *
                                      static_cast<float>(i) / sampleRate);
    return out;
}
}

TEST(FotoHoloBarkMeter, LowAndHighSpectraProduceOppositeBoundedSpatialOffsets) {
    ivanna::rt::FotoHoloBarkMeter meter;
    meter.prepare(48000.0f);
    const auto low = tone(110.0f, 48000.0f, 4800);
    const auto high = tone(6000.0f, 48000.0f, 4800);
    float delta = 0.0f;
    for (int i = 0; i < 8; ++i) delta = meter.processBlock(low.data(), low.data(), low.size(), true, 1.0f);
    EXPECT_LT(delta, 0.0f);
    EXPECT_GE(delta, -0.25f);
    EXPECT_LE(delta, 0.25f);
    for (int i = 0; i < 8; ++i) delta = meter.processBlock(high.data(), high.data(), high.size(), true, 1.0f);
    EXPECT_GT(delta, 0.0f);
    EXPECT_GE(delta, -0.25f);
    EXPECT_LE(delta, 0.25f);
}

TEST(FotoHoloBarkMeter, SilenceDisableAndInvalidInputFailClosedToZero) {
    ivanna::rt::FotoHoloBarkMeter meter;
    meter.prepare(96000.0f);
    const std::vector<float> silence(2048, 0.0f);
    EXPECT_FLOAT_EQ(meter.processBlock(silence.data(), silence.data(), silence.size(), true, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(meter.processBlock(silence.data(), silence.data(), silence.size(), false, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(meter.processBlock(nullptr, nullptr, silence.size(), true, 1.0f), 0.0f);
    EXPECT_EQ(meter.decimationStride(), 2u);
}

TEST(FotoHoloBarkMeter, IntensityScalesTheModulationAndNeverExceedsContract) {
    ivanna::rt::FotoHoloBarkMeter meter;
    meter.prepare(48000.0f);
    const auto high = tone(8000.0f, 48000.0f, 4800);
    float half = 0.0f, full = 0.0f;
    for (int i = 0; i < 8; ++i) half = meter.processBlock(high.data(), high.data(), high.size(), true, 0.5f);
    meter.reset();
    for (int i = 0; i < 8; ++i) full = meter.processBlock(high.data(), high.data(), high.size(), true, 1.0f);
    EXPECT_GT(full, 0.0f);
    EXPECT_LE(std::fabs(half), std::fabs(full) + 0.01f);
    EXPECT_LE(std::fabs(full), 0.25f);
}
