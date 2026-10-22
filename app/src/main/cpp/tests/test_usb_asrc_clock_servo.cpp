#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "usb_asrc_clock_servo.hpp"

using ivanna::audio::UsbAsrcClockServo;

TEST(UsbAsrcClockServo, EmptyAndFullRingDriveBoundedCorrection) {
    UsbAsrcClockServo servo;
    for (int i = 0; i < 10000; ++i) {
        const double correction = servo.update(0, 1024);
        EXPECT_LE(correction, 0.0);
        EXPECT_GE(correction, -UsbAsrcClockServo::kMaxCorrection);
    }
    EXPECT_NEAR(servo.correction(), -UsbAsrcClockServo::kMaxCorrection, 1e-9);

    for (int i = 0; i < 10000; ++i) {
        const double correction = servo.update(1024, 1024);
        EXPECT_GE(correction, 0.0);
        EXPECT_LE(correction, UsbAsrcClockServo::kMaxCorrection);
    }
    EXPECT_NEAR(servo.correction(), UsbAsrcClockServo::kMaxCorrection, 1e-9);
}

TEST(UsbAsrcClockServo, NominalHalfFullOccupancyConvergesToUnity) {
    UsbAsrcClockServo servo;
    for (int i = 0; i < 20000; ++i) servo.update(512, 1024);
    EXPECT_NEAR(servo.correction(), 0.0, 1e-12);
}

TEST(UsbAsrcClockServo, InvalidCapacityFailsClosedAndResetsCorrection) {
    UsbAsrcClockServo servo;
    for (int i = 0; i < 1000; ++i) servo.update(1024, 1024);
    ASSERT_GT(servo.correction(), 0.0);
    EXPECT_DOUBLE_EQ(servo.update(0, 0), 0.0);
    EXPECT_DOUBLE_EQ(servo.update(100, -1), 0.0);
}

TEST(UsbAsrcClockServo, OutOfRangeFillIsClampedAndResultRemainsFinite) {
    UsbAsrcClockServo servo;
    for (int i = 0; i < 10000; ++i) {
        const double low = servo.update(std::numeric_limits<int>::min(), 1024);
        ASSERT_TRUE(std::isfinite(low));
        ASSERT_GE(low, -UsbAsrcClockServo::kMaxCorrection);
        ASSERT_LE(low, UsbAsrcClockServo::kMaxCorrection);
    }
    EXPECT_NEAR(servo.correction(), -UsbAsrcClockServo::kMaxCorrection, 1e-9);
    for (int i = 0; i < 10000; ++i) servo.update(std::numeric_limits<int>::max(), 1024);
    EXPECT_NEAR(servo.correction(), UsbAsrcClockServo::kMaxCorrection, 1e-9);
}
