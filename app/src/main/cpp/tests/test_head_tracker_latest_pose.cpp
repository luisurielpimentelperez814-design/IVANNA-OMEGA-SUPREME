#include <gtest/gtest.h>
#include "../spatial/ivanna_head_tracker.hpp"

#include <cmath>
#include <limits>

using ivanna::spatial::HeadTracker;

namespace {
void rotationAroundZ(float degrees, float out[4]) {
    const float half = degrees * static_cast<float>(M_PI) / 360.0f;
    out[0] = 0.f;
    out[1] = 0.f;
    out[2] = std::sin(half);
    out[3] = std::cos(half);
}
} // namespace

TEST(HeadTrackerLatestPoseTest, LatestSensorPoseDoesNotDependOnAudioBlockDurationClock) {
    HeadTracker tracker;
    float zero[4]{0.f, 0.f, 0.f, 1.f};
    float yaw45[4]{};
    rotationAroundZ(45.f, yaw45);

    tracker.update(zero, 1000.f);
    tracker.update(yaw45, 1010.f);

    const auto fromLatest = tracker.getLatestPose();
    const auto fromCompatibilityApi = tracker.getPoseForAudioFrame(10.67f);
    EXPECT_NEAR(fromLatest.orientation.yawDegrees(), 45.f, 0.02f);
    EXPECT_FLOAT_EQ(fromLatest.timestampMs, 1010.f);
    EXPECT_NEAR(fromCompatibilityApi.orientation.yawDegrees(), 45.f, 0.02f);
    EXPECT_FLOAT_EQ(fromCompatibilityApi.timestampMs, 1010.f);
}

TEST(HeadTrackerLatestPoseTest, DuplicateOrOutOfOrderSamplesDoNotReplaceLatestPose) {
    HeadTracker tracker;
    float yaw30[4]{};
    float yaw90[4]{};
    rotationAroundZ(30.f, yaw30);
    rotationAroundZ(90.f, yaw90);

    tracker.update(yaw30, 500.f);
    tracker.update(yaw90, 499.f);
    tracker.update(yaw90, 500.f);

    const auto actual = tracker.getLatestPose();
    EXPECT_NEAR(actual.orientation.yawDegrees(), 30.f, 0.02f);
    EXPECT_FLOAT_EQ(actual.timestampMs, 500.f);
}

TEST(HeadTrackerLatestPoseTest, NonFiniteSensorInputIsRejected) {
    HeadTracker tracker;
    float yaw20[4]{};
    rotationAroundZ(20.f, yaw20);
    tracker.update(yaw20, 100.f);

    float invalid[4]{0.f, 0.f, std::numeric_limits<float>::quiet_NaN(), 1.f};
    tracker.update(invalid, 200.f);

    const auto actual = tracker.getLatestPose();
    EXPECT_NEAR(actual.orientation.yawDegrees(), 20.f, 0.02f);
    EXPECT_FLOAT_EQ(actual.timestampMs, 100.f);
}

TEST(HeadTrackerLatestPoseTest, ResetPublishesIdentityAndClearsTimestamp) {
    HeadTracker tracker;
    float yaw60[4]{};
    rotationAroundZ(60.f, yaw60);
    tracker.update(yaw60, 100.f);
    tracker.reset();

    const auto actual = tracker.getLatestPose();
    EXPECT_NEAR(actual.orientation.yawDegrees(), 0.f, 0.02f);
    EXPECT_FLOAT_EQ(actual.timestampMs, 0.f);
}
