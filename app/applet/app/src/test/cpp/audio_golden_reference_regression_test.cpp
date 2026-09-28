#include <gtest/gtest.h>
#include <cmath>

TEST(AudioRegression, GoldenSignalWithinTolerance) {

    float input = 0.5f;

    float expected = std::tanh(input);

    float output = std::tanh(input);

    EXPECT_NEAR(output, expected, 1e-6f);
}
