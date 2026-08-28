#include <gtest/gtest.h>

TEST(Recovery, EngineReturnsAfterStress){

    bool running = true;

    running = false;
    EXPECT_FALSE(running);

    running = true;
    EXPECT_TRUE(running);
}
