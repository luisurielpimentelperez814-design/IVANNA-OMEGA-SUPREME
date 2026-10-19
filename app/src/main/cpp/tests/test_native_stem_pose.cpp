#include <gtest/gtest.h>
#include "../neuromorphic/ivanna_neural_upmixer.hpp"

#include <array>
#include <cmath>
#include <limits>

using ivanna::ai::NeuralUpmixer;
using ivanna::ai::StemPosition;
using ivanna::ai::StemType;

namespace {
std::array<StemPosition, 4> testPose() {
    return {{
        {-1.0f, 0.0f, 1.0f, 0.20f, 1.0f},
        { 0.0f, 1.0f, 0.4f, 0.40f, 1.0f},
        { 0.8f, 0.2f,-0.3f, 0.30f, 1.0f},
        { 0.1f,-0.4f, 0.0f, 0.75f, 1.0f}
    }};
}
} // namespace

TEST(NativeStemPoseTest, BatchPublishesAllFourPositionsAndPreservesStemGains) {
    NeuralUpmixer upmixer;
    ASSERT_TRUE(upmixer.init(48000.f, 512));
    const auto requested = testPose();

    ASSERT_TRUE(upmixer.setStemPositions(requested));
    for (int i = 0; i < 4; ++i) {
        const auto actual = upmixer.getStemPosition(static_cast<StemType>(i));
        EXPECT_NEAR(actual.x, requested[i].x, 1e-6f);
        EXPECT_NEAR(actual.y, requested[i].y, 1e-6f);
        EXPECT_NEAR(actual.z, requested[i].z, 1e-6f);
        EXPECT_NEAR(actual.width, requested[i].width, 1e-6f);
        EXPECT_NEAR(actual.gain, ivanna::ai::kStemPositions[i].gain, 1e-6f);
    }
}

TEST(NativeStemPoseTest, InvalidBatchIsRejectedWithoutPartiallyChangingScene) {
    NeuralUpmixer upmixer;
    ASSERT_TRUE(upmixer.init(48000.f, 512));
    const auto valid = testPose();
    ASSERT_TRUE(upmixer.setStemPositions(valid));

    auto invalid = valid;
    invalid[2].z = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(upmixer.setStemPositions(invalid));

    for (int i = 0; i < 4; ++i) {
        const auto actual = upmixer.getStemPosition(static_cast<StemType>(i));
        EXPECT_NEAR(actual.x, valid[i].x, 1e-6f);
        EXPECT_NEAR(actual.y, valid[i].y, 1e-6f);
        EXPECT_NEAR(actual.z, valid[i].z, 1e-6f);
        EXPECT_NEAR(actual.width, valid[i].width, 1e-6f);
    }
}

TEST(NativeStemPoseTest, OutOfBoundsWidthOrCoordinatesAreRejected) {
    NeuralUpmixer upmixer;
    ASSERT_TRUE(upmixer.init(48000.f, 512));
    const auto initial = upmixer.getStemPosition(StemType::VOCALS);

    auto invalid = testPose();
    invalid[0].x = 2.1f;
    EXPECT_FALSE(upmixer.setStemPositions(invalid));
    invalid = testPose();
    invalid[1].width = 0.0f;
    EXPECT_FALSE(upmixer.setStemPositions(invalid));

    const auto after = upmixer.getStemPosition(StemType::VOCALS);
    EXPECT_NEAR(after.x, initial.x, 1e-6f);
    EXPECT_NEAR(after.y, initial.y, 1e-6f);
    EXPECT_NEAR(after.z, initial.z, 1e-6f);
    EXPECT_NEAR(after.width, initial.width, 1e-6f);
}

TEST(NativeStemPoseTest, SingleStemSetterKeepsOtherStemPositions) {
    NeuralUpmixer upmixer;
    ASSERT_TRUE(upmixer.init(48000.f, 512));
    const auto requested = testPose();
    ASSERT_TRUE(upmixer.setStemPositions(requested));

    upmixer.setStemPosition(StemType::DRUMS, 0.25f, -0.5f, 0.8f, 0.55f);
    const auto drums = upmixer.getStemPosition(StemType::DRUMS);
    EXPECT_NEAR(drums.x, 0.25f, 1e-6f);
    EXPECT_NEAR(drums.y, -0.5f, 1e-6f);
    EXPECT_NEAR(drums.z, 0.8f, 1e-6f);
    EXPECT_NEAR(drums.width, 0.55f, 1e-6f);

    const auto vocals = upmixer.getStemPosition(StemType::VOCALS);
    EXPECT_NEAR(vocals.x, requested[0].x, 1e-6f);
    EXPECT_NEAR(vocals.y, requested[0].y, 1e-6f);
    EXPECT_NEAR(vocals.z, requested[0].z, 1e-6f);
}
