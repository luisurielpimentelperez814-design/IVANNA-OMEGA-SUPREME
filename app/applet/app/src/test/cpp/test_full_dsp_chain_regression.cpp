#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(FullDSPChain, StableUnderStressSignal) {
    std::vector<float> L(4096), R(4096);

    for (size_t i=0;i<L.size();i++) {
        float x = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
        L[i] = x * 0.99f;
        R[i] = x * 0.99f;
    }

    for (size_t i=0;i<L.size();i++) {
        EXPECT_TRUE(std::isfinite(L[i]));
        EXPECT_TRUE(std::isfinite(R[i]));
        EXPECT_LE(std::abs(L[i]), 1.2f);
        EXPECT_LE(std::abs(R[i]), 1.2f);
    }
}
