#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "supreme/SpatialAngleReadback.hpp"

using ivanna::supreme::SpatialAngleReadback;

TEST(SpatialAngleReadback, PreservesFiniteAnglesInsideSupportedRange) {
    EXPECT_FLOAT_EQ(SpatialAngleReadback::appliedRadians(0.0f), 0.0f);
    EXPECT_NEAR(SpatialAngleReadback::appliedRadians(0.5f), 0.5f, 1e-7f);
    EXPECT_NEAR(SpatialAngleReadback::appliedRadians(-1.0f), -1.0f, 1e-7f);
}

TEST(SpatialAngleReadback, ClampsToNativeNinetyDegreeCommandRange) {
    EXPECT_FLOAT_EQ(SpatialAngleReadback::appliedRadians(4.0f), SpatialAngleReadback::kHalfPi);
    EXPECT_FLOAT_EQ(SpatialAngleReadback::appliedRadians(-4.0f), -SpatialAngleReadback::kHalfPi);
    EXPECT_FLOAT_EQ(SpatialAngleReadback::appliedRadians(SpatialAngleReadback::kHalfPi),
                    SpatialAngleReadback::kHalfPi);
}

TEST(SpatialAngleReadback, RejectsNonFiniteCommandsWithoutProducingAValidReadback) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(std::isfinite(SpatialAngleReadback::appliedRadians(nan)));
    EXPECT_FALSE(std::isfinite(SpatialAngleReadback::appliedRadians(inf)));
    EXPECT_FALSE(std::isfinite(SpatialAngleReadback::appliedRadians(-inf)));
}

TEST(SpatialAngleReadback, OutputIsAlwaysBoundedForFiniteInputs) {
    for (int degrees = -720; degrees <= 720; ++degrees) {
        const float radians = static_cast<float>(degrees) * 0.017453292519943295f;
        const float applied = SpatialAngleReadback::appliedRadians(radians);
        EXPECT_TRUE(std::isfinite(applied));
        EXPECT_GE(applied, -SpatialAngleReadback::kHalfPi);
        EXPECT_LE(applied, SpatialAngleReadback::kHalfPi);
    }
}
