#include "../../../../../hal_integration/ivanna_pal_active_stream_clock_publisher.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <utility>
#include <cstdlib>
#include <unistd.h>

namespace {
using ivanna::audio::HardwareClockSample;
using ivanna::audio::PalMmapPositionClockQuery;

struct FakePosition {
    int32_t frames;
    int64_t timeNs;
    bool ok = true;
};

struct FakePalMmap {
    std::deque<FakePosition> positions;
    static bool query(void* context, int32_t* frames, int64_t* timeNs) noexcept {
        auto* self = static_cast<FakePalMmap*>(context);
        if (!self || !frames || !timeNs || self->positions.empty()) return false;
        const FakePosition p = self->positions.front();
        self->positions.pop_front();
        if (!p.ok) return false;
        *frames = p.frames;
        *timeNs = p.timeNs;
        return true;
    }
};

TEST(PalMmapPositionClockQuery, ExtendsUnsigned32BitFrameWrap) {
    FakePalMmap fake;
    fake.positions = {
        {-256, 1000000000},
        {256, 1010000000}
    };
    PalMmapPositionClockQuery query(&FakePalMmap::query, &fake, 48000);
    HardwareClockSample a{}, b{};
    ASSERT_TRUE(query.query(&a));
    ASSERT_TRUE(query.query(&b));
    EXPECT_EQ(a.presentedFrames, 0xffffff00ull);
    EXPECT_EQ(b.presentedFrames, 0x100000100ull);
    EXPECT_EQ(b.presentedFrames - a.presentedFrames, 512ull);
    EXPECT_EQ(b.hardwareTimestampNs - a.hardwareTimestampNs, 10000000ull);
}

TEST(PalMmapPositionClockQuery, RejectsCounterResetOrImplausibleJump) {
    FakePalMmap fake;
    fake.positions = {
        {10000, 1000000000},
        {5000, 1010000000}, // reset/backwards jump is not a 2-second delta
        {64, 1020000000}    // next query safely re-anchors
    };
    PalMmapPositionClockQuery query(&FakePalMmap::query, &fake, 48000);
    HardwareClockSample sample{};
    ASSERT_TRUE(query.query(&sample));
    EXPECT_FALSE(query.query(&sample));
    EXPECT_TRUE(query.query(&sample));
    EXPECT_EQ(sample.presentedFrames, 64ull);
}

TEST(PalMmapPositionClockQuery, ResetsOnBadTimestampOrPalFailure) {
    FakePalMmap fake;
    fake.positions = {
        {1, 1000000000},
        {2, 1000000000}, // timestamp must advance
        {3, 1020000000, false},
        {4, 1030000000}
    };
    PalMmapPositionClockQuery query(&FakePalMmap::query, &fake, 48000);
    HardwareClockSample sample{};
    ASSERT_TRUE(query.query(&sample));
    EXPECT_FALSE(query.query(&sample));
    EXPECT_FALSE(query.query(&sample));
    EXPECT_TRUE(query.query(&sample));
    EXPECT_EQ(sample.presentedFrames, 4ull);
}

TEST(PalMmapPositionClockQuery, RejectsMissingCallbackAndInvalidRate) {
    HardwareClockSample sample{};
    PalMmapPositionClockQuery noCallback(nullptr, nullptr, 48000);
    EXPECT_FALSE(noCallback.query(&sample));

    FakePalMmap fake;
    fake.positions = {{1, 1000000000}};
    PalMmapPositionClockQuery badRate(&FakePalMmap::query, &fake, 7999);
    EXPECT_FALSE(badRate.query(&sample));
}
} // namespace
