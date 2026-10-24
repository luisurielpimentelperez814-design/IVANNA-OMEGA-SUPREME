#include <gtest/gtest.h>
#include "../spatial/PhysicalTargetDispatcher.hpp"
#include <array>
#include <cmath>
#include <limits>

using ivanna::spatial::PhysicalTargetDispatcher;

TEST(PhysicalTargetDispatcher, MapsConfiguredSixteenInputsToFiveLogicalBuses) {
    PhysicalTargetDispatcher d;
    PhysicalTargetDispatcher::Matrix m{};
    m[0 * 16 + 0] = 1.0f;
    m[1 * 16 + 1] = 1.0f;
    m[2 * 16 + 2] = 1.0f;
    m[3 * 16 + 3] = 1.0f;
    m[4 * 16 + 4] = 1.0f;
    ASSERT_TRUE(d.setMatrix(m));
    std::array<float, 16> in{};
    in[0] = 0.1f; in[1] = 0.2f; in[2] = 0.3f; in[3] = 0.4f; in[4] = 0.5f;
    std::array<float, 5> out{};
    ASSERT_TRUE(d.process(in.data(), out.data(), 1));
    EXPECT_FLOAT_EQ(out[0], 0.1f);
    EXPECT_FLOAT_EQ(out[1], 0.2f);
    EXPECT_FLOAT_EQ(out[2], 0.3f);
    EXPECT_FLOAT_EQ(out[3], 0.4f);
    EXPECT_FLOAT_EQ(out[4], 0.5f);
}

TEST(PhysicalTargetDispatcher, RejectsInvalidMatrixWithoutChangingCurrentMatrix) {
    PhysicalTargetDispatcher d;
    const auto before = d.matrix();
    auto invalid = before;
    invalid[7] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(d.setMatrix(invalid));
    EXPECT_EQ(d.matrix(), before);
}

TEST(PhysicalTargetDispatcher, ClipsOutputsAndRejectsInvalidBuffersOrGain) {
    PhysicalTargetDispatcher d;
    PhysicalTargetDispatcher::Matrix m{};
    for (std::size_t i = 0; i < 16; ++i) m[i] = 2.0f;
    ASSERT_TRUE(d.setMatrix(m));
    std::array<float, 16> in{}; in.fill(1.0f);
    std::array<float, 5> out{};
    ASSERT_TRUE(d.process(in.data(), out.data(), 1));
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FALSE(d.process(in.data(), out.data(), 1, 3.0f));
    for (float v : out) EXPECT_FLOAT_EQ(v, 0.0f);
}

TEST(PhysicalTargetDispatcher, ThermalModelRequiresVerifiedTemperature) {
    PhysicalTargetDispatcher::ThermalState unknown{};
    unknown.temperatureValid = false;
    EXPECT_FLOAT_EQ(PhysicalTargetDispatcher::thermalDerating(unknown), 1.0f);
    PhysicalTargetDispatcher::ThermalState hot{120.0f, true};
    EXPECT_NEAR(PhysicalTargetDispatcher::thermalDerating(hot), 0.25f, 1e-6f);
    float ratio = 0.0f;
    ASSERT_TRUE(PhysicalTargetDispatcher::copperResistanceRatio(120.0f, &ratio));
    EXPECT_NEAR(ratio, 1.393f, 1e-5f);
    EXPECT_FALSE(PhysicalTargetDispatcher::copperResistanceRatio(
        std::numeric_limits<float>::infinity(), &ratio));
}
