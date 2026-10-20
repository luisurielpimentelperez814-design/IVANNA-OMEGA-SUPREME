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

struct MapQuadratic {
    std::array<float, kDim> target;
    std::array<float, kDim> prior;
    float lambda;

    double objective(const std::array<float, kDim>& p) const {
        double j = 0.0;
        for (std::size_t i = 0; i < kDim; ++i) {
            const double dt = static_cast<double>(p[i]) - target[i];
            const double dp = static_cast<double>(p[i]) - prior[i];
            j += 0.5 * dt * dt + 0.5 * lambda * dp * dp;
        }
        return j;
    }

    std::array<float, kDim> exactMinimizer(const Box& box) const {
        std::array<float, kDim> p{};
        for (std::size_t i = 0; i < kDim; ++i) {
            const double closed = (target[i] + lambda * prior[i]) / (1.0 + lambda);
            p[i] = static_cast<float>(std::clamp(
                    closed, static_cast<double>(box.lower[i]),
                    static_cast<double>(box.upper[i])));
        }
        return p;
    }
};

// Faithful test-only replica of the previous iterative step, kept as a
// comparison baseline after production step() was replaced by the closed form.
std::array<float, kDim> safRoomOriginal(
        const std::array<float, kDim>& target,
        const std::array<float, kDim>& prior,
        const Box& box,
        int iterations) {
    constexpr std::array<float, kDim> g0{
        9.267955e-05f, 1.068827e-04f, 1.249753e-04f,
        1.709935e-04f, 3.144054e-04f, 7.175372e-04f, 2.897181e-03f
    };
    std::array<float, kDim> p = prior;
    constexpr float rt60 = 0.3f, mismatchEnergy = 0.0f, diffuseness = 0.0f;
    const float sigma = 0.15f * rt60 + mismatchEnergy * mismatchEnergy + 0.40f * diffuseness;
    const float lambda = 0.01f * (1.0f + sigma);
    for (int n = 0; n < iterations; ++n) {
        double error = 0.0, naturalNorm = 0.0;
        std::array<float, kDim> delta{}, metric{};
        for (std::size_t i = 0; i < kDim; ++i) {
            metric[i] = g0[i] + lambda;
            delta[i] = target[i] - p[i];
            error += static_cast<double>(delta[i]) * metric[i] * delta[i];
            naturalNorm += static_cast<double>(delta[i]) * delta[i] / metric[i];
        }
        const float alpha = static_cast<float>(
                error / (error + naturalNorm + lambda * sigma + 1.0e-8));
        for (std::size_t i = 0; i < kDim; ++i) {
            p[i] = std::clamp(p[i] + alpha * delta[i] / metric[i],
                              box.lower[i], box.upper[i]);
        }
    }
    return p;
}
}

TEST(PerceptualOptimizerV2, ClosedFormMatchesMapQuadraticExactMinimizer) {
    const auto target = filled(0.6f);
    const auto prior = filled(-0.2f);
    const auto box = unitBox();
    const auto result = solveMapQuadratic(target, prior, box, Context{0.3f, 0.0f, 0.0f});
    ASSERT_TRUE(result.finite);
    ASSERT_GT(result.lambda, 0.0f);
    const MapQuadratic objective{target, prior, result.lambda};
    const auto exact = objective.exactMinimizer(box);
    for (std::size_t i = 0; i < kDim; ++i)
        EXPECT_NEAR(result.params[i], exact[i], 1.0e-7f);
    EXPECT_NEAR(objective.objective(result.params), result.objective, 1.0e-8);
    EXPECT_LT(result.projectedKktResidual, 1.0e-6);
}

TEST(PerceptualOptimizerV2, ClosedFormBeatsLegacyIterationAgainstSameExactObjective) {
    const auto target = filled(0.6f);
    const auto prior = filled(0.0f);
    const auto box = unitBox();
    const auto result = solveMapQuadratic(target, prior, box, Context{0.3f, 0.0f, 0.0f});
    ASSERT_TRUE(result.finite);
    const MapQuadratic objective{target, prior, result.lambda};
    const auto exact = objective.exactMinimizer(box);
    const auto legacy = safRoomOriginal(target, prior, box, 200000);
    EXPECT_NEAR(objective.objective(result.params), objective.objective(exact), 1.0e-8);
    EXPECT_LE(objective.objective(result.params), objective.objective(legacy) + 1.0e-8);
    EXPECT_GT(std::abs(legacy[0] - exact[0]), 1.0e-4f);
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
