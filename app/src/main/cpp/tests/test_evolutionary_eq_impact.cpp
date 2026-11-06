#include <gtest/gtest.h>
#include "EvolutionaryEQ.hpp"
#include <algorithm>
#include <cmath>

namespace {
float responseDb(const float* taps, float omega) {
    float re = 0.0f;
    float im = 0.0f;
    for (size_t i = 0; i < Ivanna::FIR_TAPS; ++i) {
        const float phase = omega * static_cast<float>(i);
        re += taps[i] * std::cos(phase);
        im -= taps[i] * std::sin(phase);
    }
    return 20.0f * std::log10(std::max(std::sqrt(re * re + im * im), 1.0e-6f));
}
}

TEST(EvolutionaryEqImpact, CalibrationProducesTheDeclaredAudibleContour) {
    Ivanna::EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());

    const float* fir = eq.firLeft();
    ASSERT_NE(fir, nullptr);

    // Normalized angular frequencies map to approximately 458 Hz, 6.9 kHz
    // and 13.0 kHz at 48 kHz. The filter must no longer collapse to identity.
    const float lowBandDb = responseDb(fir, 0.060f);
    const float presenceDb = responseDb(fir, 0.900f);
    const float airDb = responseDb(fir, 1.700f);

    EXPECT_LT(lowBandDb, -1.0f);
    EXPECT_GT(presenceDb, 1.0f);
    EXPECT_GT(airDb, 1.5f);
    EXPECT_LT(airDb, 3.2f); // conserva margen frente al techo de +3 dB.
}
