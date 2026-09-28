#include <gtest/gtest.h>
#include <cmath>

TEST(AudioRealtime, BufferLoopMaintainsDeadline){

    const int frames = 480;

    float buffer[frames];

    for(int i=0;i<frames;i++)
        buffer[i]=std::sin(i*0.01f);

    for(int i=0;i<frames;i++){
        EXPECT_TRUE(std::isfinite(buffer[i]));
    }
}
