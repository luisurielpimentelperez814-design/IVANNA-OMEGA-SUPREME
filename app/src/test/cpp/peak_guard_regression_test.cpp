#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(PeakGuardRegression, PreventsOvershoot) {
    std::vector<float> signal(2048, 2.0f);

    float peak = 0.0f;

    for (float x : signal) {
        float limited = std::tanh(x);
        peak = std::max(peak, std::abs(limited));

        EXPECT_FALSE(std::isnan(limited));
        EXPECT_FALSE(std::isinf(limited));
    }

    EXPECT_LE(peak, 1.0f);
}
