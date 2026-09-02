#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, HandlesExtremeSignals) {

    float signals[] = {
        0.0f,
        1.0f,
        -1.0f,
        10.0f,
        -10.0f
    };

    for (float x : signals) {
        float y = std::tanh(x);

        EXPECT_FALSE(std::isnan(y));
        EXPECT_FALSE(std::isinf(y));
        EXPECT_LE(std::abs(y),1.0f);
    }
}
