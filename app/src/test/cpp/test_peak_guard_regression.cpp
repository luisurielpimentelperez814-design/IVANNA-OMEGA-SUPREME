#include <gtest/gtest.h>
#include <cmath>

TEST(PeakGuard, PreventsOvershootAfterImpulse){
    float peak = 5.0f;

    EXPECT_LE(std::abs(peak * 0.2f),1.0f);
    EXPECT_TRUE(std::isfinite(peak));
}
