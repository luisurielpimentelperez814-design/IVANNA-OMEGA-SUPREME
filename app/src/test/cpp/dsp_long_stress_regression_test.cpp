#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, LongRunStability) {
    float state = 0.0f;

    for (int block = 0; block < 100000; block++) {
        float input = std::sin(block * 0.001f);

        state = state * 0.99f + input * 0.01f;

        EXPECT_FALSE(std::isnan(state));
        EXPECT_FALSE(std::isinf(state));
        EXPECT_LE(std::abs(state), 1.0f);
    }
}
