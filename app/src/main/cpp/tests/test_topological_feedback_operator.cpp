#include <gtest/gtest.h>

#include <limits>

#include "../include/topological_feedback_operator.hpp"

namespace {
using ivanna::topological_feedback::Operator;
using ivanna::topological_feedback::Vector;
}

TEST(TopologicalFeedbackOperator, ProducesBoundedPositiveGeometry) {
    Operator op;
    const auto state = op.observe(Vector{1.0, 0.8, 0.6, 0.4, 0.2, 0.1, 0.05});
    ASSERT_TRUE(state.valid);
    EXPECT_GE(state.trustRadius, 0.025);
    EXPECT_LE(state.trustRadius, 0.10);
    for (double scale : state.metricScale) {
        EXPECT_TRUE(std::isfinite(scale));
        EXPECT_GE(scale, 0.5);
        EXPECT_LE(scale, 2.0);
    }
}

TEST(TopologicalFeedbackOperator, IsotropicResidualKeepsMaximumTrustRadius) {
    Operator op;
    const auto state = op.observe(Vector{1, 1, 1, 1, 1, 1, 1});
    ASSERT_TRUE(state.valid);
    EXPECT_NEAR(state.trustRadius, 0.10, 1e-12);
    EXPECT_NEAR(state.anisotropy, 1.0, 1e-12);
}

TEST(TopologicalFeedbackOperator, RejectsNonFiniteObservationWithoutChangingState) {
    Operator op;
    const auto before = op.observe(Vector{1, 1, 1, 1, 1, 1, 1});
    Vector bad{1, 1, 1, 1, 1, 1, 1};
    bad[3] = std::numeric_limits<double>::quiet_NaN();
    const auto after = op.observe(bad);
    EXPECT_EQ(after.valid, before.valid);
    EXPECT_DOUBLE_EQ(after.trustRadius, before.trustRadius);
    EXPECT_EQ(after.metricScale, before.metricScale);
}

TEST(TopologicalFeedbackOperator, LargeAnisotropyTightensTrustRegion) {
    Operator op;
    const auto isotropic = op.observe(Vector{1, 1, 1, 1, 1, 1, 1});
    const auto anisotropic = op.observe(Vector{10, 0.01, 0.01, 0.01, 0.01, 0.01, 0.01});
    ASSERT_TRUE(anisotropic.valid);
    EXPECT_LT(anisotropic.trustRadius, isotropic.trustRadius);
}
