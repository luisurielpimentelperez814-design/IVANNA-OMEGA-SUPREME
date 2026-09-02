#include <gtest/gtest.h>

TEST(Certification, ThermalWindow){

    float operating_temp = 45.0f;
    float limit_temp = 85.0f;

    EXPECT_LT(operating_temp,limit_temp);
}
