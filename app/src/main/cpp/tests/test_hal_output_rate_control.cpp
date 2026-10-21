#include "include/hal_output_rate_control.hpp"
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
    EXPECT_TRUE(control.update({4800, 1100000000ull, 48000}));
    EXPECT_TRUE(control.update({9601, 1200000000ull, 48000}));
    EXPECT_TRUE(control.update({14402, 1300000000ull, 48000}));
    EXPECT_TRUE(control.enabled());
    EXPECT_NE(control.lastAppliedRatio(), 1.0);
    EXPECT_GT(asrc.ratios.size(), 3u);
}
TEST(HalOutputRateControl, InvalidReferenceReturnsAsrcToUnity) {
    FakeAsrc asrc;
    ivanna::audio::HalOutputRateControl control(&FakeAsrc::apply, &asrc);
    control.update({0, 1000000000ull, 48000});
    control.update({4800, 1100000000ull, 48000});
    control.update({9601, 1200000000ull, 48000});
    control.update({14402, 1300000000ull, 48000});
    ASSERT_TRUE(control.enabled());
    EXPECT_TRUE(control.update({15000, 1200000000ull, 48000}));
    EXPECT_FALSE(control.enabled());
    EXPECT_DOUBLE_EQ(asrc.ratios.back(), 1.0);
}
TEST(HalOutputRateControl, AsrcRejectionFailsClosed) {
    FakeAsrc asrc;
    ivanna::audio::HalOutputRateControl control(&FakeAsrc::apply, &asrc);
    control.update({0, 1000000000ull, 48000});
    control.update({4800, 1100000000ull, 48000});
    control.update({9601, 1200000000ull, 48000});
    asrc.accept = false;
    EXPECT_FALSE(control.update({14402, 1300000000ull, 48000}));
    EXPECT_FALSE(control.enabled());
}
} // namespace
