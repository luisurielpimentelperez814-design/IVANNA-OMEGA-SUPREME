// test_cheb_shaper.cpp — Prueba T7: Excitador Armónico por Polinomios de Chebyshev (M9)
#include <gtest/gtest.h>
#include "../dsp/ChebHarmonicShaper.hpp"
#include <cmath>
#include <vector>

namespace {

using namespace ivanna::dsp;

// Proyección de Fourier (Goertzel / correlación exacta) en frecuencia fHz
float measureHarmonicMag(const std::vector<float>& y, size_t startIdx, float fHz, float sr) {
    double re = 0.0, im = 0.0;
    const size_t count = y.size() - startIdx;
    for (size_t i = startIdx; i < y.size(); ++i) {
        const double ph = 6.283185307179586 * static_cast<double>(fHz) * static_cast<double>(i) / static_cast<double>(sr);
        re += static_cast<double>(y[i]) * std::cos(ph);
        im += static_cast<double>(y[i]) * std::sin(ph);
    }
    return static_cast<float>(2.0 * std::sqrt(re * re + im * im) / static_cast<double>(count));
}

TEST(ChebShaperTest, WarmthControlsEvenVsOddHarmonicsAndRemovesDcOffset) {
    constexpr float kSr = 48000.0f;
    constexpr size_t N  = 24000; // 0.5 s para que el bloqueador DC de 15 Hz converja por completo
    constexpr size_t kStart = 12000;

    std::vector<float> in(N);
    for (size_t i = 0; i < N; ++i) {
        // Seno de 1 kHz a -6 dBFS (0.5)
        in[i] = 0.5f * std::sin(6.283185307179586f * 1000.0f * static_cast<float>(i) / kSr);
    }

    // Caso 1: warmth = 1.0, drive = 0.5 -> H2 > H3 por >= 3 dB y |DC| < 1e-4
    ChebHarmonicShaper shaperWarm;
    shaperWarm.prepare(kSr);
    std::vector<float> outWarm(N);
    for (size_t i = 0; i < N; ++i) {
        outWarm[i] = shaperWarm.tick(in[i], 0.5f, 1.0f, 0.0f);
    }

    const float h2Warm = measureHarmonicMag(outWarm, kStart, 2000.0f, kSr);
    const float h3Warm = measureHarmonicMag(outWarm, kStart, 3000.0f, kSr);
    const float ratioWarmDb = 20.0f * std::log10((h2Warm + 1e-12f) / (h3Warm + 1e-12f));
    EXPECT_GE(ratioWarmDb, 3.0f)
        << "Con warmth=1.0, H2 debe superar a H3 en >= 3 dB (medido: " << ratioWarmDb << " dB)";

    double dcSum = 0.0;
    for (size_t i = kStart; i < N; ++i) dcSum += outWarm[i];
    const float dcMean = static_cast<float>(std::fabs(dcSum / static_cast<double>(N - kStart)));
    EXPECT_LT(dcMean, 1.0e-4f) << "Offset DC en la salida debe ser < 1e-4 (medido: " << dcMean << ")";

    // Caso 2: warmth = 0.0, drive = 0.5 -> H3 > H2
    ChebHarmonicShaper shaperCrisp;
    shaperCrisp.prepare(kSr);
    std::vector<float> outCrisp(N);
    for (size_t i = 0; i < N; ++i) {
        outCrisp[i] = shaperCrisp.tick(in[i], 0.5f, 0.0f, 0.0f);
    }
    const float h2Crisp = measureHarmonicMag(outCrisp, kStart, 2000.0f, kSr);
    const float h3Crisp = measureHarmonicMag(outCrisp, kStart, 3000.0f, kSr);
    EXPECT_GT(h3Crisp, h2Crisp)
        << "Con warmth=0.0, H3 (" << h3Crisp << ") debe superar a H2 (" << h2Crisp << ")";

    // Caso 3: Anti-IMD (flatness1m = 1.0 reduce la excitación armónica un 50%)
    ChebHarmonicShaper shaperDense;
    shaperDense.prepare(kSr);
    std::vector<float> outDense(N);
    for (size_t i = 0; i < N; ++i) {
        outDense[i] = shaperDense.tick(in[i], 0.5f, 1.0f, 1.0f);
    }
    const float h2Dense = measureHarmonicMag(outDense, kStart, 2000.0f, kSr);
    EXPECT_LT(h2Dense, h2Warm * 0.65f) << "Anti-IMD (f8=1.0) debe reducir los armónicos generados";
}

} // namespace
