#include <gtest/gtest.h>

class DummyDSPState {
public:
    float state = 0.0f;

    void process(float x) {
        state = state * 0.9f + x * 0.1f;
    }

    void reset() {
        state = 0.0f;
    }
};


TEST(DSPRegression, ResetClearsPreviousState) {
    DummyDSPState dsp;

    dsp.process(1.0f);
    EXPECT_GT(dsp.state, 0.0f);

    dsp.reset();

    EXPECT_FLOAT_EQ(dsp.state, 0.0f);
}
