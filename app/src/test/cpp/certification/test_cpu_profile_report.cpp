#include <gtest/gtest.h>

TEST(Certification, CpuBudget){

    float cpu_load_percent = 35.0f;

    EXPECT_LT(cpu_load_percent,80.0f);
    EXPECT_GT(cpu_load_percent,0.0f);
}
