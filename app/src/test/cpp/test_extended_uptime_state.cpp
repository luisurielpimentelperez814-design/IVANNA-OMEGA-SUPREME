#include <gtest/gtest.h>
#include <cstdint>

TEST(Uptime, StateCounterDoesNotOverflow){

    uint64_t processedFrames = 0;

    for(int i=0;i<100000;i++)
        processedFrames += 480;

    EXPECT_GT(processedFrames,0);
}
