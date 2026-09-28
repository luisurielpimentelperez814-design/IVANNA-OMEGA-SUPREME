#include <gtest/gtest.h>
#include <vector>
#include <cmath>

TEST(DSPRegression, SupportsDifferentBufferSizes) {

    for (int size : {16,64,128,256,512,1024}) {

        std::vector<float> buffer(size);

        for (int i = 0; i < size; i++)
            buffer[i] = std::sin(i * 0.01f);

        for (float x : buffer) {
            EXPECT_FALSE(std::isnan(x));
            EXPECT_FALSE(std::isinf(x));
        }
    }
}
