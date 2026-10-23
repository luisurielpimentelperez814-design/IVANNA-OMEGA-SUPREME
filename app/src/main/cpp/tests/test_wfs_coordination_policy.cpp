#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "wfs_coordination_policy.hpp"

namespace {

TEST(WfsCoordinationPolicy, NeutralSpreadPreservesBoundedAdaptiveScale) {
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(0.9f, 1.0f), 0.9f);
}

TEST(WfsCoordinationPolicy, WiderSpreadOnlyAttenuates) {
    const float scale = ivanna::unity::coordinatedWfsSafetyScale(1.0f, 1.35f);
    EXPECT_NEAR(scale, 1.0f / 1.35f, 1.0e-6f);
    EXPECT_GE(scale, 0.5f);
    EXPECT_LE(scale, 1.0f);
}

TEST(WfsCoordinationPolicy, InputsAreBounded) {
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(9.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(-4.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(1.0f, 99.0f), 1.0f / 1.35f);
}

TEST(WfsCoordinationPolicy, NonFiniteInputsCannotPoisonScale) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(nan, 1.2f), 1.0f / 1.2f);
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(inf, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(0.8f, nan), 0.8f);
    EXPECT_FLOAT_EQ(ivanna::unity::coordinatedWfsSafetyScale(0.8f, inf), 0.8f);
}

TEST(WfsCoordinationPolicy, ResultAlwaysRespectsSafetyContract) {
    const float values[] = {-100.0f, -1.0f, 0.0f, 0.5f, 1.0f, 1.35f, 100.0f,
                            std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()};
    for (float adaptive : values) {
        for (float spread : values) {
            const float scale = ivanna::unity::coordinatedWfsSafetyScale(adaptive, spread);
            EXPECT_TRUE(std::isfinite(scale));
            EXPECT_GE(scale, 0.5f);
            EXPECT_LE(scale, 1.0f);
        }
    }
}

} // namespace
