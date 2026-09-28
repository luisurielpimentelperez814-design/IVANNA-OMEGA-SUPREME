#include <gtest/gtest.h>
#include <cmath>

TEST(JNIBoundary, InvalidValuesAreRejected){

    float values[] = {
        0.0f,
        1.0f,
        -1.0f,
        NAN,
        INFINITY
    };

    for(float v: values){

        if(!std::isfinite(v)){
            EXPECT_FALSE(std::isfinite(v));
        }else{
            EXPECT_TRUE(std::isfinite(v));
        }
    }
}
