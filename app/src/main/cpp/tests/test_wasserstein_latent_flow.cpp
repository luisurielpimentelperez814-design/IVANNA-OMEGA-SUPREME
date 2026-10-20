#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include "include/wasserstein_latent_flow.hpp"

namespace {
using ivanna::wasserstein_latent_flow::Vector;
TEST(WassersteinLatentFlow, PreservesMassAndMovesTowardTarget) {
    Vector current{1,0,0,0,0,0,0};
    Vector target{0,0,0,0,0,0,2};
    const auto result = ivanna::wasserstein_latent_flow::transportTarget(current,target,0.25);
    ASSERT_TRUE(result.valid);
    EXPECT_NEAR(result.targetMass,2.0,1e-12);
    double mass=0.0;
    for(double x:result.target) mass+=std::abs(x);
    EXPECT_NEAR(mass,2.0,1e-12);
    EXPECT_GT(std::abs(result.target[1])+std::abs(result.target[2]),0.0);
    EXPECT_GT(std::abs(result.target[5])+std::abs(result.target[6]),0.0);
}
TEST(WassersteinLatentFlow, FullStepMatchesTargetAndZeroSourceIsSafe) {
    Vector current{};
    Vector target{0.1,-0.2,0.0,0.0,0.0,0.0,0.3};
    auto result=ivanna::wasserstein_latent_flow::transportTarget(current,target,0.15);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.target,target);
    result=ivanna::wasserstein_latent_flow::transportTarget(Vector{1,0,0,0,0,0,0},target,1.0);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.target,target);
}
TEST(WassersteinLatentFlow, RejectsNonFiniteInputs) {
    Vector current{}, target{};
    target[2]=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(ivanna::wasserstein_latent_flow::transportTarget(current,target).valid);
}
}
