#include <gtest/gtest.h>
#include <cmath>

TEST(ParameterSmoothing, NoAbruptJump){
    float previous = 0.0f;

    for(int i=0;i<100;i++){
        float current = previous + (1.0f-previous)*0.05f;
        EXPECT_LT(std::abs(current-previous),0.1f);
        previous=current;
    }
}
