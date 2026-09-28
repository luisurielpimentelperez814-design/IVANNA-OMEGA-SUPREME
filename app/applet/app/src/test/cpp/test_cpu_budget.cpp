#include <gtest/gtest.h>
#include <chrono>
#include <cmath>

TEST(CPU, ProcessingRemainsBounded){

    auto start = std::chrono::high_resolution_clock::now();

    volatile float acc=0;

    for(int i=0;i<1000000;i++){
        acc += std::sin(i*0.001f);
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end-start).count();

    EXPECT_LT(elapsed,500);

    EXPECT_TRUE(std::isfinite(acc));
}
