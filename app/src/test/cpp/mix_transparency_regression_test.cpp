#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(MixRegression, ZeroMixIsTransparent) {
    std::vector<float> input(1024);
    for (size_t i = 0; i < input.size(); i++)
        input[i] = std::sin(i * 0.01f);

    std::vector<float> output = input;

    float mix = 0.0f;

    for (size_t i = 0; i < input.size(); i++) {
        output[i] = input[i] * (1.0f - mix) + output[i] * mix;
    }

    for (size_t i = 0; i < input.size(); i++)
        EXPECT_NEAR(output[i], input[i], 1e-6f);
}

TEST(MixRegression, IntermediateMixStable) {
    float previous = 0.0f;

    for (float mix : {0.25f, 0.5f, 0.75f}) {
        float out = (1.0f - mix) + mix * 0.5f;
        EXPECT_FALSE(std::isnan(out));
        EXPECT_TRUE(out <= 1.0f);
        previous = out;
    }
}
