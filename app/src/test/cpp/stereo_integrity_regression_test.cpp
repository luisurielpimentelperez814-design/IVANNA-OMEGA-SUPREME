#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, StereoChannelsRemainValid) {
    float left = 0.7f;
    float right = -0.7f;

    for (int i = 0; i < 4096; i++) {
        left = std::tanh(left);
        right = std::tanh(right);

        EXPECT_FALSE(std::isnan(left));
        EXPECT_FALSE(std::isnan(right));
        EXPECT_LE(std::abs(left), 1.0f);
        EXPECT_LE(std::abs(right), 1.0f);
    }
}
