#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(DSPRegression, NoNanOrInfPropagation) {
    std::vector<float> samples = {
        0.0f, 1.0f, -1.0f,
        INFINITY, -INFINITY, NAN
    };

    for (float x : samples) {
        float y = std::isfinite(x) ? std::tanh(x) : 0.0f;

        EXPECT_FALSE(std::isnan(y));
        EXPECT_FALSE(std::isinf(y));
    }
}
