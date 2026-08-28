#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(DSPChainRegression, FullChainNoInvalidSamples) {
    std::vector<float> buffer(4096);

    for (size_t i = 0; i < buffer.size(); i++)
        buffer[i] = std::sin(i * 0.02f);

    for (float &x : buffer) {
        // simulación cadena DSP:
        x *= 1.2f;
        x = std::tanh(x);

        EXPECT_FALSE(std::isnan(x));
        EXPECT_FALSE(std::isinf(x));
        EXPECT_LE(std::abs(x), 1.0f);
    }
}
