#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#include "include/saf_room_optimizer.hpp"

namespace {

TEST(SaFRoomOptimizerProduction, NaturalStepKeepsParametersFiniteAndBounded) {
    ivanna::SaFRoomOptimizer optimizer;
    const float target[ivanna::SAFR_K] = {
        0.020f, -0.021f, 0.018f, -0.025f, 0.030f, -0.040f, 0.080f
    };
    optimizer.setTarget(target);

    const float weight = optimizer.step();
    EXPECT_TRUE(std::isfinite(weight));
    EXPECT_GT(weight, 0.0f);
    EXPECT_EQ(optimizer.getIteration(), 1);

    std::array<float, ivanna::SAFR_K> params{};
    optimizer.getParams(params.data());
    for (int i = 0; i < ivanna::SAFR_K; ++i) {
        const auto k = static_cast<std::size_t>(i);
        EXPECT_TRUE(std::isfinite(params[k]));
        EXPECT_GE(params[k], ivanna::kSAFR_Pmin[i]);
        EXPECT_LE(params[k], ivanna::kSAFR_Pmax[i]);
    }
}

TEST(SaFRoomOptimizerProduction, RejectedNonFiniteTargetPreservesLastSafeState) {
    ivanna::SaFRoomOptimizer optimizer;
    std::array<float, ivanna::SAFR_K> before{};
    std::array<float, ivanna::SAFR_K> after{};
    optimizer.getParams(before.data());

    float target[ivanna::SAFR_K] = {};
    target[3] = std::numeric_limits<float>::quiet_NaN();
    optimizer.setTarget(target);

    EXPECT_FLOAT_EQ(optimizer.step(), 0.0f);
    optimizer.getParams(after.data());
    EXPECT_EQ(after, before);
    EXPECT_EQ(optimizer.getIteration(), 0);
}

TEST(SaFRoomOptimizerProduction, RepeatedStepsApproachBoundedMapTarget) {
    ivanna::SaFRoomOptimizer optimizer;
    const float target[ivanna::SAFR_K] = {
        0.010f, -0.012f, 0.014f, -0.016f, 0.020f, -0.025f, 0.050f
    };
    optimizer.setTarget(target);
    for (int i = 0; i < 40; ++i) EXPECT_GT(optimizer.step(), 0.0f);

    std::array<float, ivanna::SAFR_K> params{};
    optimizer.getParams(params.data());
    for (int i = 0; i < ivanna::SAFR_K; ++i) {
        EXPECT_NEAR(params[static_cast<std::size_t>(i)], target[i], 2.0e-3f);
    }
    EXPECT_EQ(optimizer.getIteration(), 40);
}

} // namespace
