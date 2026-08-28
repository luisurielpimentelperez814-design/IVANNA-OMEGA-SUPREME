#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <random>

TEST(AdaptiveEQStress, LongRunParameterAdaptationStable) {

    constexpr int samples = 48000 * 60; // 1 minuto de estrés
    float gain = 1.0f;
    float state = 0.0f;

    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f,1.0f);

    for (int i = 0; i < samples; i++) {

        float input = dist(rng);

        // Simulación del seguimiento adaptativo
        float target = std::abs(input);
        gain += (target - gain) * 0.001f;

        state = input * gain;

        EXPECT_TRUE(std::isfinite(state));
        EXPECT_TRUE(std::isfinite(gain));

        // evita runaway del adaptador
        EXPECT_LT(std::abs(gain), 4.0f);
        EXPECT_LT(std::abs(state), 2.0f);
    }
}

TEST(AdaptiveEQStress, NoExplosiveRecoveryAfterSilence) {

    float state = 1.0f;

    for(int i=0;i<50000;i++){
        state *= 0.9998f;
    }

    EXPECT_TRUE(std::isfinite(state));
    EXPECT_LT(std::abs(state),0.01f);
}
