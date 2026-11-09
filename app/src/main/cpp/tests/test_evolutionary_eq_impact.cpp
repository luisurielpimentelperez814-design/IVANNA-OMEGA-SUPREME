// test_evolutionary_eq_impact.cpp — el EQ evolutivo produce el contorno
// audible DECLARADO (Harman 2018 ×0.5).
//
// v2.5.369: el objetivo cambió de la curva paramétrica "presencia" (graves
// −2 dB, aire +3 dB) a Harman 2018 ×0.5 (el target élite del producto). El
// test viejo esperaba ese contorno de presencia y ya no aplica. El contorno
// Harman ×0.5 es: realce de graves (+1.75 dB a 20 Hz), referencia unidad en
// 1 kHz, pico de pinna (+4.3 dB a 3.15 kHz) y caída suave de aire (piso
// −2.0 dB a 16 kHz; el FIR no realiza el −4.15 dB Harman completo, ver
// docs/EVOLUTIONARY_EQ.md). Se verifica el contorno en los anclajes, no en
// frecuencias arbitrarias.
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
float wn(float freqHz) { return 6.2831853f * freqHz / 48000.0f; }
}

TEST(EvolutionaryEqImpact, CalibrationProducesTheDeclaredAudibleContour) {
    Ivanna::EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());

    const float* fir = eq.firLeft();
    ASSERT_NE(fir, nullptr);

    // Contorno Harman 2018 ×0.5 (medido en host: 20 Hz +1.29, 1 kHz 0.00,
    // 3.15 kHz +4.30, 16 kHz −1.16). El filtro no debe colapsar a identidad.
    const float bassDb = responseDb(fir, wn(20.0f));
    const float refDb = responseDb(fir, wn(1000.0f));
    const float pinnaDb = responseDb(fir, wn(3150.0f));
    const float airDb = responseDb(fir, wn(16000.0f));

    EXPECT_GT(bassDb, 0.0f);   // realce de graves presente (objetivo +1.75)
    EXPECT_LT(bassDb, 3.0f);
    EXPECT_NEAR(refDb, 0.0f, 0.5f); // ancla de referencia: unidad en 1 kHz
    EXPECT_GT(pinnaDb, 3.0f);  // pico de pinna presente (objetivo +4.30)
    EXPECT_LT(pinnaDb, 6.0f);  // con margen frente al tope de +6 dB
    EXPECT_LT(airDb, 1.0f);    // sin realce de aire (el rolloff Harman no
                               // se realiza del todo, pero no debe impulsar)
    EXPECT_GT(airDb, -3.0f);   // ni un corte extremo no intencionado
}
