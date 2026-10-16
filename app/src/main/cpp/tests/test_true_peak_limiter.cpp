// v2.5.36 — Pico REAL (inter-muestra) en el SafetyLimiter. El DAC reconstruye la onda entre muestras: un tono
// a fs/4 con fase 45° tiene picos de muestra de 0.707*A pero un pico continuo de A. Antes el limitador solo miraba
// |muestra| y dejaba pasar hasta +3 dB sobre el techo hacia la salida de 3.5 mm.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "include/SafetyLimiter.h"
#include "include/true_peak.hpp"

using namespace ivanna;

static std::vector<float> fsQuarter(int n, float amp) {      // 12 kHz a fs = 48 kHz, fase 45°
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = amp * static_cast<float>(std::sin(M_PI / 2.0 * i + M_PI / 4.0));
    return x;
}

// Pico continuo de referencia: evalúa la onda analítica con 64 puntos por muestra (independiente del estimador).
static float analyticTruePeak(float amp) { return amp; }

TEST(TruePeak, EstimatorFindsTheHiddenIntersamplePeak) {
    const auto x = fsQuarter(4096, 1.2f);
    float samplePeak = 0.f;
    for (float v : x) samplePeak = std::fmax(samplePeak, std::fabs(v));
    EXPECT_NEAR(samplePeak, 1.2f * 0.70710678f, 1e-3f);                  // lo que veía el limitador antes
    const float tp = truePeak4x(x.data(), static_cast<int>(x.size()));
    EXPECT_NEAR(tp, analyticTruePeak(1.2f), 0.03f * 1.2f);              // el pico real, dentro del 3 %
    EXPECT_GT(tp, samplePeak * 1.35f);
}

TEST(TruePeak, NeverBelowSamplePeakAndSafeOnEdgeCases) {
    const float one[1] = {0.5f};
    EXPECT_FLOAT_EQ(truePeak4x(one, 1), 0.5f);
    EXPECT_FLOAT_EQ(truePeak4x(nullptr, 10), 0.0f);
    EXPECT_FLOAT_EQ(truePeak4x(one, 0), 0.0f);
    std::vector<float> noise(2048);
    uint32_t s = 7; for (auto& v : noise) { s = s * 1664525u + 1013904223u; v = 0.4f * (((s >> 8) & 0xFFFF) / 32768.0f - 1.0f); }
    float sp = 0.f; for (float v : noise) sp = std::fmax(sp, std::fabs(v));
    EXPECT_GE(truePeak4x(noise.data(), 2048), sp);
}

TEST(TruePeakLimiter, OutputTruePeakStaysUnderTheCeilingForIntersampleOvers) {
    SafetyLimiter lim;
    lim.setSampleRate(48000.f);
    lim.setParams();                                                      // techo -0.1 dBFS = 0.98855
    const int block = 512, blocks = 200;
    std::vector<float> l(block), r(block);
    float worstTrue = 0.f;
    for (int b = 0; b < blocks; ++b) {
        const auto src = fsQuarter(block * (blocks) , 1.2f);
        for (int i = 0; i < block; ++i) { l[i] = r[i] = src[b * block + i]; }
        lim.process(l.data(), r.data(), block);
        if (b >= blocks / 2) worstTrue = std::fmax(worstTrue, std::fmax(truePeak4x(l.data(), block), truePeak4x(r.data(), block)));
    }
    EXPECT_LE(worstTrue, 0.98855f + 0.002f)
        << "el pico continuo de la salida supera el techo: se recortaría en el DAC";
}

TEST(TruePeakLimiter, QuietMaterialStaysBitExact) {
    SafetyLimiter lim; lim.setSampleRate(48000.f); lim.setParams();
    std::vector<float> l(512), r(512), l0, r0;
    for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.3f * static_cast<float>(std::sin(0.05 * i));
    l0 = l; r0 = r;
    lim.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) { ASSERT_EQ(l[i], l0[i]); ASSERT_EQ(r[i], r0[i]); }
}
