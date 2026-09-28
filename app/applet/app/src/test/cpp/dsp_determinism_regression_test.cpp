#include <gtest/gtest.h>
#include <cmath>
#include <vector>

static float process(float x) {
    return std::tanh(x * 1.2f);
}

TEST(DSPRegression, SameInputSameOutput) {

    std::vector<float> a(1024);
    std::vector<float> b(1024);

    for(int i=0;i<1024;i++) {
        a[i] = process(std::sin(i * 0.01f));
        b[i] = process(std::sin(i * 0.01f));
    }

    for(int i=0;i<1024;i++)
        EXPECT_NEAR(a[i], b[i], 1e-7f);
}
