#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include "include/perceptual_optimizer_v2.hpp"

namespace {
using namespace ivanna::perceptual_v2;

Box unitBox() {
    Box box{};
    box.lower.fill(-1.0f);
    box.upper.fill(1.0f);
    return box;
}
std::array<float, kDim> filled(float x) {
    std::array<float, kDim> a{};
    a.fill(x);
    return a;
}
}

TEST(PerceptualOptimizerV2, ClosedFormMatchesAnalyticMapMinimizerInOneSolve) {
    const auto target = filled(0.6f);
    const auto prior = filled(-0.2f);
    const auto result = solveMapQuadratic(target, prior, unitBox(), Context{0.3f, 0.0f, 0.0f});
    ASSERT_TRUE(result.finite);
    ASSERT_GT(result.lambda, 0.0f);
    for (std::size_t i = 0; i < kDim; ++i) {
        const float exact = (target[i] + result.lambda * prior[i]) / (1.0f + result.lambda);
        EXPECT_NEAR(result.params[i], exact, 1.0e-7f);
    }
    EXPECT_LT(result.projectedKktResidual, 1.0e-6);
}

TEST(PerceptualOptimizerV2, BoxConstraintProducesConstrainedOptimum) {
    auto target = filled(2.0f);
    const auto prior = filled(0.0f);
    auto box = unitBox();
    box.lower.fill(-0.25f);
    box.upper.fill(0.4f);
    const auto result = solveMapQuadratic(target, prior, box, Context{});
    ASSERT_TRUE(result.finite);
    for (const auto p : result.params) {
        EXPECT_GE(p, -0.25f);
        EXPECT_LE(p, 0.4f);
        EXPECT_NEAR(p, 0.4f, 1.0e-7f);
    }
    EXPECT_LT(result.projectedKktResidual, 1.0e-6);
}

TEST(PerceptualOptimizerV2, ExponentialContextRegularizationIsPositiveAndMonotonic) {
    const auto target = filled(0.5f);
    const auto prior = filled(0.0f);
    const auto box = unitBox();
    const auto easy = solveMapQuadratic(target, prior, box, Context{0.1f, 0.0f, 0.0f});
    const auto hard = solveMapQuadratic(target, prior, box, Context{5.0f, 1.0f, 1.0f});
    ASSERT_TRUE(easy.finite);
    ASSERT_TRUE(hard.finite);
    EXPECT_GT(easy.lambda, 0.0f);
    EXPECT_GT(hard.lambda, easy.lambda);
    EXPECT_GT(hard.contextExponent, easy.contextExponent);
}

TEST(PerceptualOptimizerV2, RejectsNonFiniteTargetAndInvalidBounds) {
    auto target = filled(0.5f);
    const auto prior = filled(0.0f);
    auto box = unitBox();
    target[3] = NAN;
    EXPECT_FALSE(solveMapQuadratic(target, prior, box, Context{}).finite);

    target = filled(0.5f);
    box.lower[2] = 2.0f;
    box.upper[2] = -2.0f;
    EXPECT_FALSE(solveMapQuadratic(target, prior, box, Context{}).finite);
}
