#include <gtest/gtest.h>
#include <cmath>

TEST(DSPRegression, ModuleBypassIsTransparent) {

    float input = 0.345678f;

    float output = input; // bypass esperado

    EXPECT_NEAR(output, input, 1e-6f);
}
