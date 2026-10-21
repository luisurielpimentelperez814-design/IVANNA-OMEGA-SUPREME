#include <gtest/gtest.h>
#include <cmath>
#include "include/audio_callback_clock.hpp"

using ivanna::audio::CallbackClockMonitor;

TEST(CallbackClockMonitor, MeasuresNominalCallbackCadence) {
    CallbackClockMonitor monitor;
    constexpr uint64_t startNs = 1000000000ull;
    constexpr uint64_t stepNs = 10000000ull;
    for (uint64_t i = 0; i <= 10; ++i) {
        const auto state = monitor.observe(startNs + i * stepNs, 480, 48000);
        if (i < 10) EXPECT_FALSE(state.valid);
        else {
            ASSERT_TRUE(state.valid);
            EXPECT_NEAR(state.observedSampleRateHz, 48000.0, 1e-6);
            EXPECT_NEAR(state.driftPpm, 0.0, 1e-3);
        }
    }
}

TEST(CallbackClockMonitor, DetectsCallbackCadenceDriftWithoutChangingAudio) {
    CallbackClockMonitor monitor;
    constexpr uint64_t startNs = 2000000000ull;
    constexpr uint64_t stepNs = 10000100ull; // ~10 ppm slower than nominal cadence
    ivanna::audio::CallbackClockStatus state;
    for (uint64_t i = 0; i <= 10; ++i) {
        state = monitor.observe(startNs + i * stepNs, 480, 48000);
    }
    ASSERT_TRUE(state.valid);
    EXPECT_LT(state.driftPpm, 0.0);
    EXPECT_GT(state.driftPpm, -100.0);
}

TEST(CallbackClockMonitor, RejectsInvalidAndNonMonotonicObservations) {
    CallbackClockMonitor monitor;
    EXPECT_FALSE(monitor.observe(0, 480, 48000).valid);
    EXPECT_FALSE(monitor.observe(1000, 480, 48000).valid);
    EXPECT_FALSE(monitor.observe(900, 480, 48000).valid);
    EXPECT_FALSE(monitor.observe(2000, 480, 48000).valid);
}
