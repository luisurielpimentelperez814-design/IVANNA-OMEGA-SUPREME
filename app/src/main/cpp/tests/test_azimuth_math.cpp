#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "azimuth_math.hpp"

using ivanna::spatial::wrapAzimuthDegrees;

TEST(AzimuthMath, PreservesFrontAndRearHemisphere) {
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(90.0f), 90.0f);
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(-90.0f), -90.0f);
    EXPECT_FLOAT_EQ(std::fabs(wrapAzimuthDegrees(135.0f)), 135.0f);
    EXPECT_FLOAT_EQ(std::fabs(wrapAzimuthDegrees(-135.0f)), 135.0f);
}

TEST(AzimuthMath, WrapsHeadRotationAcrossFullCircle) {
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(270.0f), -90.0f);
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(450.0f), 90.0f);
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(-450.0f), -90.0f);
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(720.0f), 0.0f);
}

TEST(AzimuthMath, NonFiniteInputFailsClosed) {
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_FLOAT_EQ(wrapAzimuthDegrees(std::numeric_limits<float>::infinity()), 0.0f);
}
