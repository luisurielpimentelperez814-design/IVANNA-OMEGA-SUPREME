#include <gtest/gtest.h>
#include "SaFOptimizer.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
class SafStateTest : public ::testing::Test {
protected:
    std::filesystem::path file;
    void SetUp() override {
        std::string pattern = (std::filesystem::temp_directory_path() / "ivanna-saf-XXXXXX").string();
        int fd = mkstemp(pattern.data());
        ASSERT_GE(fd, 0);
        close(fd);
        file = pattern;
    }
    void TearDown() override { std::filesystem::remove(file); std::filesystem::remove(file.string() + ".tmp"); }
};
TEST_F(SafStateTest, RoundTripPreservesEveryFloatAndIteration) {
    Ivanna::SaFOptimizer source, restored;
    for (int i = 0; i < 17; ++i) source.feedFeedback(i % 5, false);
    ASSERT_TRUE(source.saveState(file.c_str()));
    ASSERT_TRUE(restored.loadState(file.c_str()));
    std::array<float, 7> a{}, b{};
    source.getParams(a.data()); restored.getParams(b.data());
    EXPECT_EQ(a, b); EXPECT_EQ(source.getIteration(), restored.getIteration());
}
TEST_F(SafStateTest, MalformedSnapshotsNeverMutateActiveState) {
    Ivanna::SaFOptimizer optimizer;
    optimizer.feedFeedback(1, false);
    std::array<float, 7> before{}, after{};
    optimizer.getParams(before.data());
    for (const std::string& payload : {
        "", "IVANNA_SAF_STATE_V1\n4\n0 0", "IVANNA_SAF_STATE_V1\n-1\n0 0 0 0 0 0 0",
        "IVANNA_SAF_STATE_V1\n3\n0 0 nan 0 0 0 0", "IVANNA_SAF_STATE_V1\n3\n0 0 999 0 0 0 0",
        "IVANNA_SAF_STATE_V1\n3\n0 0 0 0 0 0 0 extra"}) {
        std::ofstream(file) << payload;
        EXPECT_FALSE(optimizer.loadState(file.c_str())) << payload;
        optimizer.getParams(after.data());
        EXPECT_EQ(before, after); EXPECT_EQ(optimizer.getIteration(), 1);
    }
}
TEST_F(SafStateTest, CompleteSnapshotWithoutFinalNewlineIsAccepted) {
    std::ofstream(file) << "IVANNA_SAF_STATE_V1\n3\n0 0 0 0 0 0 0";
    Ivanna::SaFOptimizer optimizer;
    EXPECT_TRUE(optimizer.loadState(file.c_str()));
    EXPECT_EQ(optimizer.getIteration(), 3);
}
TEST(SafNaturalGradient, ActualUpdateStaysWithinHalfSigmaTrustRegion) {
    constexpr float variance[7] = {9.267955e-5f, 1.068827e-4f, 1.249753e-4f,
        1.709935e-4f, 3.144054e-4f, 7.175372e-4f, 2.897181e-3f};
    Ivanna::SaFOptimizer optimizer;
    optimizer.reset();
    for (int step = 0; step < 500; ++step) {
        std::array<float, 7> before{}, after{};
        optimizer.getParams(before.data());
        optimizer.feedFeedback(step % 5, false);
        optimizer.getParams(after.data());
        double norm2 = 0;
        for (int i = 0; i < 7; ++i) {
            ASSERT_TRUE(std::isfinite(after[i]));
            EXPECT_LE(std::abs(after[i]), 3.001 * std::sqrt(variance[i]));
            const double d = after[i] - before[i];
            norm2 += d * d / variance[i];
        }
        EXPECT_LE(std::sqrt(norm2), .50001) << step;
    }
}
}
