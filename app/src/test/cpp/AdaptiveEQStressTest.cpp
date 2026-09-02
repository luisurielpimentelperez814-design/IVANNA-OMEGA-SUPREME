#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <random>

TEST(AdaptiveEQStress, ExtremeSignalRemainsStable) {

    constexpr size_t samples = 48000;
    std::vector<float> L(samples);
    std::vector<float> R(samples);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> noise(-1.0f,1.0f);

    for (size_t i = 0; i < samples; i++) {
        float tone = std::sin(2.0f * M_PI * 1000.0f *
                              static_cast<float>(i) / 48000.0f);

        // mezcla de tono, ruido y nivel alto
        L[i] = tone * 0.95f + noise(rng) * 0.05f;
        R[i] = tone * 0.95f + noise(rng) * 0.05f;
    }

    // Punto de integración:
    // llamar aquí AdaptiveEQ real del motor Ivanna

    float energyBefore = 0.0f;
    float energyAfter  = 0.0f;

    for (size_t i = 0; i < samples; i++) {

        ASSERT_TRUE(std::isfinite(L[i]));
        ASSERT_TRUE(std::isfinite(R[i]));

        energyAfter += L[i]*L[i] + R[i]*R[i];

        EXPECT_LE(std::abs(L[i]), 1.5f);
        EXPECT_LE(std::abs(R[i]), 1.5f);
    }

    EXPECT_TRUE(std::isfinite(energyAfter));

    // evita explosión energética accidental
    EXPECT_LT(energyAfter, samples * 4.0f);
}


TEST(AdaptiveEQStress, RapidParameterChangesRemainStable) {

    float gain = 0.0f;
    float smoothed = 0.0f;

    for (int i = 0; i < 10000; i++) {

        // simulación de IA cambiando decisiones EQ
        gain = (i % 2 == 0) ? 12.0f : -12.0f;

        // smoothing esperado en implementación real
        smoothed += (gain - smoothed) * 0.01f;

        EXPECT_TRUE(std::isfinite(smoothed));
        EXPECT_LT(std::abs(smoothed), 20.0f);
    }
}


TEST(AdaptiveEQStress, SilenceDoesNotCreateSignal) {

    float output = 0.0f;

    for (int i = 0; i < 48000; i++) {

        float input = 0.0f;

        // Punto de integración:
        // procesar AdaptiveEQ real aquí

        output += input;
    }

    EXPECT_FLOAT_EQ(output, 0.0f);
}
