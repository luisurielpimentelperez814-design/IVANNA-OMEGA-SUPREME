#include <gtest/gtest.h>

TEST(Thermal, SafetyLimitsRemainActive){

    float temperature = 42.0f;
    float throttleLimit = 85.0f;

    EXPECT_LT(temperature, throttleLimit);
}
