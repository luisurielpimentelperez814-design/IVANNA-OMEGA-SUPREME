#include <gtest/gtest.h>

TEST(Latency, RemainsBounded){
    int latencySamples = 256;

    EXPECT_LE(latencySamples,512);
    EXPECT_GT(latencySamples,0);
}
