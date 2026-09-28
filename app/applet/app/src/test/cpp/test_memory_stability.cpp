#include <gtest/gtest.h>
#include <vector>

TEST(Memory, RepeatedAllocationDoesNotLeak){

    size_t initial = 0;

    for(int i=0;i<1000;i++){

        std::vector<float> buffer(4096);

        buffer[0]=1.0f;

        EXPECT_EQ(buffer.size(),4096);
    }

    EXPECT_EQ(initial,0);
}
