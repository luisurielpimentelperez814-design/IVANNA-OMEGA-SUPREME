#include <gtest/gtest.h>
#include <cmath>

TEST(IIRState, SilenceReturnsToStableState) {
    float state = 1.0f;

    for(int i=0;i<20000;i++){
        state *= 0.9995f;
    }

    EXPECT_LT(std::abs(state),0.01f);
    EXPECT_TRUE(std::isfinite(state));
}
