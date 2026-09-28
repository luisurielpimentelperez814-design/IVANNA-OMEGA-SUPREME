#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, DynamicParameterChangesAreStable) {
    float wet = 0.0f;
    float target = 1.0f;

    float previous = wet;

    for (int i = 0; i < 1000; i++) {
        wet += (target - wet) * 0.01f;

        EXPECT_FALSE(std::isnan(wet));
        EXPECT_FALSE(std::isinf(wet));
        EXPECT_GE(wet, previous);

        previous = wet;
    }

    EXPECT_NEAR(wet, target, 0.01f);
}
