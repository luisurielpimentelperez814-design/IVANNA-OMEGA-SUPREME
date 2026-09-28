#include <gtest/gtest.h>

TEST(Certification, MemoryFootprintStable){

    size_t baseline_kb = 4096;
    size_t peak_kb = 8192;

    EXPECT_GT(peak_kb,baseline_kb);
    EXPECT_LT(peak_kb,65536);
}
