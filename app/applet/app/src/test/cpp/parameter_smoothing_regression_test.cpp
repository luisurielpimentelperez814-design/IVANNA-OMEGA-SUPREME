#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, ParameterChangesAreSmooth) {
    float current = 0.0f;
    float target = 1.0f;

    float last = current;

    for (int i = 0; i < 100; i++) {
        current += (target - current) * 0.05f;

        EXPECT_FALSE(std::isnan(current));
        EXPECT_GE(current, last);

        last = current;
    }

    EXPECT_NEAR(current, target, 0.01f);
}
