#include "include/audio_hardware_clock.hpp"

#include <gtest/gtest.h>

namespace {

TEST(HardwareClockTracker, EstimatesDriverReportedPresentationRate) {
    ivanna::audio::HardwareClockTracker tracker;
    EXPECT_FALSE(tracker.observe({0, 1000000000ull, 48000}).valid);
    const auto status = tracker.observe({48000, 1999990000ull, 48000});
    ASSERT_TRUE(status.valid);
    EXPECT_NEAR(status.measuredSampleRateHz, 48000.48, 0.02);
    EXPECT_NEAR(status.driftPpm, 10.0, 0.5);
}

TEST(HardwareClockTracker, RejectsNonMonotonicHardwareSnapshots) {
    ivanna::audio::HardwareClockTracker tracker;
    EXPECT_FALSE(tracker.observe({100, 1000000000ull, 48000}).valid);
    EXPECT_FALSE(tracker.observe({200, 1000000000ull, 48000}).valid);
    // A reset is required after invalid ordering; the next sample only primes it.
    EXPECT_FALSE(tracker.observe({300, 1100000000ull, 48000}).valid);
}

TEST(HardwareClockTracker, RejectsInvalidRateAndLongGaps) {
    ivanna::audio::HardwareClockTracker tracker;
    EXPECT_FALSE(tracker.observe({1, 1000000000ull, 1000}).valid);
    EXPECT_FALSE(tracker.observe({1, 1000000000ull, 48000}).valid);
    EXPECT_FALSE(tracker.observe({48001, 5000000000ull, 48000}).valid);
}

} // namespace
