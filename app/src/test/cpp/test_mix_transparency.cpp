#include <gtest/gtest.h>
#include <cmath>

TEST(Mix, ZeroWetIsTransparent){
    float dry = 0.54321f;
    float wet = 0.0f;

    float out = dry*(1.0f-wet)+dry*wet;

    EXPECT_FLOAT_EQ(out,dry);
}
