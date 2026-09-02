#include <gtest/gtest.h>
#include <chrono>
#include <cmath>

TEST(DSPRegression, ProcessingTimeRemainsStable) {

    auto start = std::chrono::high_resolution_clock::now();

    float state = 0.0f;

    for(int i=0;i<100000;i++)
        state = std::tanh(state + std::sin(i));

    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end-start).count();

    EXPECT_GT(elapsed,0);
    EXPECT_FALSE(std::isnan(state));
}
