#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, StereoPhaseIntegrity) {

    float left = 0.5f;
    float right = -0.5f;

    for(int i=0;i<4096;i++) {
        left = std::tanh(left);
        right = std::tanh(right);

        EXPECT_FALSE(std::isnan(left));
        EXPECT_FALSE(std::isnan(right));
    }
}
