#include <gtest/gtest.h>
#include <vector>
#include <cmath>

static float simple_iir(float x, float &state) {
    state = state * 0.9f + x * 0.1f;
    return state;
}

TEST(IIRRegression, StatePersistsAcrossBlocks) {
    std::vector<float> input(1024, 1.0f);

    float stateA = 0.0f;
    float stateB = 0.0f;

    std::vector<float> continuous;
    std::vector<float> blocks;

    for (float x : input)
        continuous.push_back(simple_iir(x, stateA));

    for (size_t i = 0; i < input.size(); i++) {
        if (i == 512)
            ; // simula cambio de buffer sin reset

        blocks.push_back(simple_iir(input[i], stateB));
    }

    for (size_t i = 0; i < input.size(); i++)
        EXPECT_NEAR(blocks[i], continuous[i], 1e-6f);
}
