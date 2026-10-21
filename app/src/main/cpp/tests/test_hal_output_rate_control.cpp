#include "include/hal_output_rate_control.hpp"
#include "../../../../../hal_integration/ivanna_hal_output_stream.hpp"
#include <gtest/gtest.h>
#include <vector>

namespace {
struct FakeAsrc {
    std::vector<double> ratios;
    bool accept = true;
    static bool apply(void* p, double ratio) noexcept {
        auto* self = static_cast<FakeAsrc*>(p);
        self->ratios.push_back(ratio);
        return self->accept;
    }
};

TEST(HalOutputRateControl, AppliesRatioOnlyAfterValidLock) {
    FakeAsrc asrc;
    ivanna::audio::HalOutputRateControl control(&FakeAsrc::apply, &asrc);
    EXPECT_TRUE(control.update({0, 1000000000ull, 48000}));
    EXPECT_FALSE(control.enabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(control.lastAppliedRatio(), 1.0);
    EXPECT_TRUE(control.update({4800, 1100000000ull, 48000}));
    EXPECT_TRUE(control.update({9601, 1200000000ull, 48000}));
    EXPECT_TRUE(control.update({14402, 1300000000ull, 48000}));
    EXPECT_TRUE(control.enabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_NE(control.lastAppliedRatio(), 1.0);
    EXPECT_GT(asrc.ratios.size(), 3u);
}

TEST(HalOutputRateControl, InvalidReferenceReturnsAsrcToUnity) {
    FakeAsrc asrc;
    ivanna::audio::HalOutputRateControl control(&FakeAsrc::apply, &asrc);
    ASSERT_TRUE(control.update({0, 1000000000ull, 48000}));
    ASSERT_TRUE(control.update({4800, 1100000000ull, 48000}));
    ASSERT_TRUE(control.update({9601, 1200000000ull, 48000}));
    ASSERT_TRUE(control.update({14402, 1300000000ull, 48000}));
    ASSERT_TRUE(control.enabled());
    EXPECT_TRUE(control.update({15000, 1200000000ull, 48000}));
    EXPECT_FALSE(control.enabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(asrc.ratios.back(), 1.0);
    EXPECT_DOUBLE_EQ(control.lastAppliedRatio(), 1.0);
}

TEST(HalOutputRateControl, AsrcRejectionFailsClosedAndMarksRatioUnknown) {
    FakeAsrc asrc;
    ivanna::audio::HalOutputRateControl control(&FakeAsrc::apply, &asrc);
    ASSERT_TRUE(control.update({0, 1000000000ull, 48000}));
    ASSERT_TRUE(control.update({4800, 1100000000ull, 48000}));
    ASSERT_TRUE(control.update({9601, 1200000000ull, 48000}));
    asrc.accept = false;
    EXPECT_FALSE(control.update({14402, 1300000000ull, 48000}));
    EXPECT_FALSE(control.enabled());
    EXPECT_FALSE(control.hasKnownAppliedRatio());
}
} // namespace

namespace {
struct FakeHal {
    ivanna::audio::HardwareClockSample sample{0, 1000000000ull, 48000};
    bool queryOk = true;
    FakeAsrc asrc;
    static bool query(void* p, ivanna::audio::HardwareClockSample* out) noexcept {
        auto* self = static_cast<FakeHal*>(p);
        if (!self->queryOk || !out) return false;
        *out = self->sample;
        return true;
    }
};
TEST(HalOutputStreamClockControl, QueriesRealSampleAndAppliesThroughAsrcCallback) {
    FakeHal hal;
    ivanna::audio::HalOutputStreamClockControl control(
        &FakeHal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    EXPECT_TRUE(control.controlTick());
    hal.sample = {4800, 1100000000ull, 48000};
    EXPECT_TRUE(control.controlTick());
    hal.sample = {9601, 1200000000ull, 48000};
    EXPECT_TRUE(control.controlTick());
    hal.sample = {14402, 1300000000ull, 48000};
    EXPECT_TRUE(control.controlTick());
    EXPECT_TRUE(control.correctionEnabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_NE(control.lastAppliedRatio(), 1.0);
}
TEST(HalOutputStreamClockControl, QueryFailureResetsAndCommandsUnity) {
    FakeHal hal;
    ivanna::audio::HalOutputStreamClockControl control(
        &FakeHal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    ASSERT_TRUE(control.controlTick());
    hal.sample = {4800, 1100000000ull, 48000}; ASSERT_TRUE(control.controlTick());
    hal.sample = {9601, 1200000000ull, 48000}; ASSERT_TRUE(control.controlTick());
    hal.sample = {14402, 1300000000ull, 48000}; ASSERT_TRUE(control.controlTick());
    ASSERT_TRUE(control.correctionEnabled());
    hal.queryOk = false;
    EXPECT_FALSE(control.controlTick()); // query failed; reset still commands unity
    EXPECT_FALSE(control.correctionEnabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(control.lastAppliedRatio(), 1.0);
    EXPECT_DOUBLE_EQ(hal.asrc.ratios.back(), 1.0);
}
} // namespace


TEST(HalOutputStreamClockControl, SupportsCommonInternalAndUsbDacRates) {
    // Exercise the clock-control contract at rates commonly exposed by Android
    // output routes and USB DACs. This is a host test of the controller only;
    // the real device HAL must still provide paired hardware timestamps and a
    // callback that changes the actual converter.
    constexpr uint32_t rates[] = {
        44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 768000
    };
    for (const uint32_t rate : rates) {
        FakeHal hal;
        hal.sample = {0, 1000000000ull, rate};
        ivanna::audio::HalOutputStreamClockControl control(
            &FakeHal::query, &hal, &FakeAsrc::apply, &hal.asrc);
        ASSERT_TRUE(control.controlTick()) << "rate=" << rate;
        for (uint64_t i = 1; i <= 3; ++i) {
            const auto frames = static_cast<uint64_t>(
                static_cast<double>(rate) * 0.1 * 1.0001 * static_cast<double>(i));
            hal.sample = {frames, 1000000000ull + i * 100000000ull, rate};
            ASSERT_TRUE(control.controlTick()) << "rate=" << rate << " sample=" << i;
        }
        EXPECT_TRUE(control.correctionEnabled()) << "rate=" << rate;
        EXPECT_TRUE(control.hasKnownAppliedRatio()) << "rate=" << rate;
        EXPECT_GT(control.lastAppliedRatio(), 0.9995) << "rate=" << rate;
        EXPECT_LT(control.lastAppliedRatio(), 1.0005) << "rate=" << rate;
    }
}


TEST(HalOutputStreamClockControl, InvalidTimestampOrRateNeverEnablesCorrection) {
    FakeHal hal;
    ivanna::audio::HalOutputStreamClockControl control(
        &FakeHal::query, &hal, &FakeAsrc::apply, &hal.asrc);

    hal.sample = {0, 0, 48000};
    EXPECT_TRUE(control.controlTick()); // query succeeded; invalid sample falls back to unity
    EXPECT_FALSE(control.correctionEnabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(control.lastAppliedRatio(), 1.0);

    hal.sample = {0, 1000000000ull, 7000};
    EXPECT_TRUE(control.controlTick());
    EXPECT_FALSE(control.correctionEnabled());

    // A valid sequence acquires lock, but a timestamp discontinuity must
    // reset the servo instead of leaving a stale correction active.
    hal.sample = {0, 2000000000ull, 48000};
    ASSERT_TRUE(control.controlTick());
    hal.sample = {4800, 2100000000ull, 48000};
    ASSERT_TRUE(control.controlTick());
    hal.sample = {9601, 2200000000ull, 48000};
    ASSERT_TRUE(control.controlTick());
    hal.sample = {14402, 2300000000ull, 48000};
    ASSERT_TRUE(control.controlTick());
    ASSERT_TRUE(control.correctionEnabled());

    hal.sample = {15000, 2200000000ull, 48000}; // time went backwards
    EXPECT_TRUE(control.controlTick()); // timestamp regression resets correction to unity
    EXPECT_FALSE(control.correctionEnabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(control.lastAppliedRatio(), 1.0);
}
