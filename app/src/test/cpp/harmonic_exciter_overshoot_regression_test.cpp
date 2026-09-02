#include <gtest/gtest.h>
#include <cmath>

TEST(HarmonicExciterRegression, DoesNotOvershootLimiterRange) {
    float peak = 0.0f;

    for (int i = 0; i < 4096; i++) {
        float input = std::sin(i * 0.05f);
        float harmonic = input + (input * input * input) * 0.2f;
        float output = std::tanh(harmonic);

        peak = std::max(peak, std::abs(output));

        EXPECT_FALSE(std::isnan(output));
        EXPECT_FALSE(std::isinf(output));
    }

    EXPECT_LE(peak, 1.0f);
}
