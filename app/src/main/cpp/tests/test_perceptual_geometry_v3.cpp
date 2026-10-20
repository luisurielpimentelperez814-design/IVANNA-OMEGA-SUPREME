#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "../include/perceptual_geometry_v3.hpp"

namespace {
using namespace ivanna::perceptual_geometry_v3;

Matrix diagonalMetric() {
    Matrix g{};
    for (std::size_t i = 0; i < kDim; ++i) g[i][i] = 1.0 + static_cast<double>(i);
    return g;
}
}

TEST(PerceptualGeometryV3, NaturalGradientMatchesDiagonalClosedForm) {
    const Matrix g = diagonalMetric();
    Vector grad{};
    for (std::size_t i = 0; i < kDim; ++i) grad[i] = 2.0 * (i + 1.0);
    const auto result = boundedNaturalDescent(g, grad, 100.0);
    ASSERT_TRUE(result.valid);
    EXPECT_DOUBLE_EQ(result.damping, 0.0);
    for (std::size_t i = 0; i < kDim; ++i)
        EXPECT_NEAR(result.descent[i], -grad[i] / g[i][i], 1.0e-12);
}

TEST(PerceptualGeometryV3, TrustRegionCapsFisherNorm) {
    const Matrix g = diagonalMetric();
    Vector grad{};
    grad.fill(100.0);
    const auto result = boundedNaturalDescent(g, grad, 0.25);
    ASSERT_TRUE(result.valid);
    EXPECT_NEAR(result.metricNorm, 0.25, 1.0e-12);
}

TEST(PerceptualGeometryV3, RejectsAsymmetricMetric) {
    Matrix g = diagonalMetric();
    g[0][1] = 0.4;
    Vector grad{};
    grad.fill(1.0);
    EXPECT_FALSE(boundedNaturalDescent(g, grad, 1.0).valid);
}

TEST(PerceptualGeometryV3, RejectsNonFiniteInput) {
    Matrix g = diagonalMetric();
    Vector grad{};
    grad.fill(1.0);
    grad[3] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(boundedNaturalDescent(g, grad, 1.0).valid);
}

TEST(PerceptualGeometryV3, SingularMetricCanBeDampedSafely) {
    Matrix g{};
    Vector grad{};
    grad.fill(1.0);
    const auto result = boundedNaturalDescent(g, grad, 0.1);
    ASSERT_TRUE(result.valid);
    EXPECT_GT(result.damping, 0.0);
    EXPECT_NEAR(result.metricNorm, 0.1, 1.0e-12);
}

TEST(PerceptualGeometryV3, StronglyIndefiniteMetricFailsClosed) {
    Matrix g = diagonalMetric();
    g[0][0] = -1.0;
    Vector grad{};
    grad.fill(1.0);
    EXPECT_FALSE(boundedNaturalDescent(g, grad, 1.0).valid);
}
