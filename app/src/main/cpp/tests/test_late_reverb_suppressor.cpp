// test_late_reverb_suppressor.cpp — Prueba T4: Supresor Estadístico de Cola Difusa (M5)
#include <gtest/gtest.h>
#include "../spatial/LateReverbSuppressor.hpp"
#include <cmath>
#include <vector>

namespace {

using namespace ivanna::spatial;

// (a) Impulso seco x[0] = 1, x[n>0] = 0 -> salida en [0..64] idéntica (< 1e-4)
TEST(LateReverbSuppressorTest, DryImpulseEarlyWindowIsUnaltered) {
    LateReverbSuppressor sup;
    sup.prepare(48000.0f);

    constexpr size_t N = 256;
    std::vector<float> L(N, 0.0f), R(N, 0.0f);
    L[0] = 1.0f;
    R[0] = -0.8f;
    const std::vector<float> origL = L, origR = R;

    sup.process(L.data(), R.data(), N, 0.75f);

    for (size_t i = 0; i <= 64; ++i) {
        EXPECT_NEAR(L[i], origL[i], 1.0e-4f) << "Impulso seco alterado en muestra i=" << i;
        EXPECT_NEAR(R[i], origR[i], 1.0e-4f) << "Impulso seco alterado en muestra i=" << i;
    }
}

// (b) Cola exponencial (RT60 = 1.2 s) sin nuevos transitorios -> lateRatio() > 0.5 tras 80 ms;
//     energía en 80–400 ms reducida >= 4 dB con strength = 0.7.
TEST(LateReverbSuppressorTest, ExponentialTailIsDetectedAndAttenuatedByAtLeast4dB) {
    constexpr float kSr = 48000.0f;
    constexpr size_t kTotal = static_cast<size_t>(0.45f * kSr); // 450 ms
    const size_t idx80ms  = static_cast<size_t>(0.080f * kSr);
    const size_t idx400ms = static_cast<size_t>(0.400f * kSr);

    std::vector<float> inL(kTotal), inR(kTotal);
    for (size_t i = 0; i < kTotal; ++i) {
        const float t = static_cast<float>(i) / kSr;
        // Decaimiento reverberante RT60 = 1.2 s: exp(-6.9078 * t / 1.2)
        const float env = std::exp(-6.907755f * t / 1.2f);
        // Campo difuso de alta frecuencia (> 500 Hz) descorrelacionado L/R
        const float carrierL = std::sin(6.2831853f * 950.0f * t)
                             + 0.7f * std::cos(6.2831853f * 2150.0f * t)
                             + 0.5f * std::sin(6.2831853f * 3700.0f * t);
        const float carrierR = std::cos(6.2831853f * 950.0f * t)
                             - 0.7f * std::sin(6.2831853f * 2150.0f * t)
                             + 0.5f * std::cos(6.2831853f * 3700.0f * t);
        inL[i] = 0.4f * env * carrierL;
        inR[i] = 0.4f * env * carrierR;
    }

    std::vector<float> outL = inL, outR = inR;
    LateReverbSuppressor sup;
    sup.prepare(kSr);

    // Procesar hasta 80 ms y verificar lateRatio() > 0.5
    sup.process(outL.data(), outR.data(), idx80ms + 256, 0.7f);
    EXPECT_GT(sup.lateRatio(), 0.50f) << "lateRatio debe superar 0.5 tras 80 ms de cola reverberante";

    // Procesar el resto hasta 450 ms
    sup.process(outL.data() + idx80ms + 256,
                outR.data() + idx80ms + 256,
                kTotal - (idx80ms + 256), 0.7f);

    double eIn = 0.0, eOut = 0.0;
    for (size_t i = idx80ms; i < idx400ms; ++i) {
        eIn  += static_cast<double>(inL[i])  * inL[i]  + static_cast<double>(inR[i])  * inR[i];
        eOut += static_cast<double>(outL[i]) * outL[i] + static_cast<double>(outR[i]) * outR[i];
    }
    const double reductionDb = 10.0 * std::log10((eIn + 1e-20) / (eOut + 1e-20));
    EXPECT_GE(reductionDb, 4.0) << "Reducción de cola en 80-400 ms debe ser >= 4 dB (medido: "
                                << reductionDb << " dB)";
}

// (c) Tono puro de 60 Hz: variación de nivel < 0.2 dB (banda < 250 Hz intacta)
TEST(LateReverbSuppressorTest, SubBass60HzRemainsIntactWithin0Point2dB) {
    constexpr float kSr = 48000.0f;
    constexpr size_t N  = 9600; // 200 ms
    std::vector<float> inL(N), inR(N);
    for (size_t i = 0; i < N; ++i) {
        const float s = 0.5f * std::sin(6.2831853f * 60.0f * static_cast<float>(i) / kSr);
        inL[i] = s;
        inR[i] = -s; // Incluso en antifase para forzar el caso más exigente
    }
    std::vector<float> outL = inL, outR = inR;

    LateReverbSuppressor sup;
    sup.prepare(kSr);
    sup.process(outL.data(), outR.data(), N, 0.75f);

    double eIn = 0.0, eOut = 0.0;
    for (size_t i = N / 2; i < N; ++i) {
        eIn  += static_cast<double>(inL[i])  * inL[i];
        eOut += static_cast<double>(outL[i]) * outL[i];
    }
    const double diffDb = std::fabs(10.0 * std::log10((eOut + 1e-20) / (eIn + 1e-20)));
    EXPECT_LT(diffDb, 0.20) << "Tono de 60 Hz varió más de 0.2 dB: " << diffDb << " dB";
}

} // namespace
