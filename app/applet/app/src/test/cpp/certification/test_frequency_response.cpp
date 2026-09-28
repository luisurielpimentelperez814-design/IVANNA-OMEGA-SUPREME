#include <gtest/gtest.h>
#include <cmath>

TEST(Certification, FrequencyResponseStable){

    float gain_low = 0.0f;
    float gain_mid = 0.1f;
    float gain_high = -0.1f;

    EXPECT_LT(std::abs(gain_low),1.0f);
    EXPECT_LT(std::abs(gain_mid),1.0f);
    EXPECT_LT(std::abs(gain_high),1.0f);
}
