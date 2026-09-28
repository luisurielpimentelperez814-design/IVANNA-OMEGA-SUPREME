#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "../dsp/AdaptiveEQ.h"

TEST(AdaptiveEQStress, LongRunningSignalStability) {

    AdaptiveEQ eq;

    constexpr int samples = 480000; // 10 segundos @48kHz

    std::vector<float> input(samples);
    std::vector<float> output(samples);

    for (int i = 0; i < samples; i++) {
        float t = static_cast<float>(i) / 48000.0f;

        // Señal compleja: mezcla tonos + variación dinámica
        input[i] =
            0.6f * sinf(2.0f * M_PI * 440.0f * t) +
            0.3f * sinf(2.0f * M_PI * 3000.0f * t);
    }

    for (int i = 0; i < samples; i++) {
        output[i] = eq.process(input[i]);

        EXPECT_TRUE(std::isfinite(output[i]));

        EXPECT_LT(fabs(output[i]), 2.0f);
    }

    auto state = eq.getState();

    EXPECT_TRUE(std::isfinite(state.gain));
    EXPECT_TRUE(std::isfinite(state.energy));
}


TEST(AdaptiveEQStress, ParameterMovementDoesNotExplode) {

    AdaptiveEQ eq;

    for(int i=0;i<10000;i++){

        float freq = 100.0f + i * 0.5f;
        float gain = sinf(i*0.01f);

        eq.setFrequency(freq);
        eq.setGain(gain);

        float out = eq.process(0.5f);

        EXPECT_TRUE(std::isfinite(out));
        EXPECT_LT(fabs(out),2.0f);
    }
}
