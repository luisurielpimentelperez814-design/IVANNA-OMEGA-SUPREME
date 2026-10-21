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
    EXPECT_FALSE(control.controlTick());
    EXPECT_FALSE(control.correctionEnabled());
    EXPECT_TRUE(control.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(control.lastAppliedRatio(), 1.0);
    EXPECT_DOUBLE_EQ(hal.asrc.ratios.back(), 1.0);
}
} // namespace
