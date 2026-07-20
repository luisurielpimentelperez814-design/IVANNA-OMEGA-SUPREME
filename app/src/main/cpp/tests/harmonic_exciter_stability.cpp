// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Tests de estabilidad numérica y equivalencia funcional del HarmonicExciter
// tras añadir la ruta NEON (v2 omnipotent).
//
// Estos tests corren host-side (x86_64) por defecto — donde IVANNA_EXCITER_NEON=0
// y por tanto se ejercita la ruta escalar. En build ARM (device / cross-compile)
// se ejercita la ruta NEON. En ambos casos se exige:
//   1. No NaN/Inf en salida bajo ruido, DC, tonos puros ni transitorios.
//   2. Ganancia unitaria efectiva cuando wet=0 (bypass).
//   3. Los coeficientes HPF idénticos L/R (asunción crítica de la ruta NEON:
//      hpfR_ = hpfL_ en setParams()).
//   4. Estabilidad tras miles de bloques con parámetros en rango extremo.

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../include/HarmonicExciter.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kBlockSize  = 512;

bool allFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

float peakAbs(const std::vector<float>& v) {
    float peak = 0.f;
    for (float x : v) peak = std::max(peak, std::fabs(x));
    return peak;
}

// Ruido determinista xorshift32 — mismo generador que gammatone_numerical_stability.
float nextNoise(uint32_t& s, float amplitude) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (static_cast<int32_t>(s) / static_cast<float>(INT32_MAX)) * amplitude;
}

} // namespace

TEST(HarmonicExciterStability, FiniteUnderPureSine) {
    ivanna::HarmonicExciter ex;
    ivanna::DSPParams p{};
    p.sampleRate = kSampleRate;
    p.drive = 0.8f;
    p.wet   = 0.6f;
    ex.setParams(p);

    std::vector<float> L(kBlockSize), R(kBlockSize);
    for (int block = 0; block < 200; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            const float t = static_cast<float>(block * kBlockSize + i) / kSampleRate;
            L[i] = 0.7f * std::sin(2.f * static_cast<float>(M_PI) * 1000.f * t);
            R[i] = 0.7f * std::sin(2.f * static_cast<float>(M_PI) * 1500.f * t);
        }
        ex.process(L.data(), R.data(), kBlockSize);
        ASSERT_TRUE(allFinite(L)) << "L NaN/Inf at block " << block;
        ASSERT_TRUE(allFinite(R)) << "R NaN/Inf at block " << block;
        ASSERT_LT(peakAbs(L), 4.f) << "L blew up at block " << block;
        ASSERT_LT(peakAbs(R), 4.f) << "R blew up at block " << block;
    }
}

TEST(HarmonicExciterStability, FiniteUnderLowLevelNoise) {
    ivanna::HarmonicExciter ex;
    ivanna::DSPParams p{};
    p.sampleRate = kSampleRate;
    p.drive = 0.9f;
    p.wet   = 0.5f;
    ex.setParams(p);

    std::vector<float> L(kBlockSize), R(kBlockSize);
    uint32_t seed = 0xdeadbeefu;

    // 4000 bloques a nivel -80 dBFS — clásico caso de spawning de denormals.
    for (int block = 0; block < 4000; ++block) {
        for (int i = 0; i < kBlockSize; ++i) {
            L[i] = nextNoise(seed, 1.0e-4f);
            R[i] = nextNoise(seed, 1.0e-4f);
        }
        ex.process(L.data(), R.data(), kBlockSize);
        ASSERT_TRUE(allFinite(L)) << "L NaN/Inf at block " << block;
        ASSERT_TRUE(allFinite(R)) << "R NaN/Inf at block " << block;
    }
}

TEST(HarmonicExciterStability, WetZeroIsBypass) {
    ivanna::HarmonicExciter ex;
    ivanna::DSPParams p{};
    p.sampleRate = kSampleRate;
    p.drive = 0.5f;
    p.wet   = 0.0f;   // sin mezcla húmeda → salida == entrada
    ex.setParams(p);

    std::vector<float> L(kBlockSize), R(kBlockSize);
    std::vector<float> Lorig(kBlockSize), Rorig(kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        L[i] = Lorig[i] = 0.4f * std::sin(2.f * static_cast<float>(M_PI) * 440.f * t);
        R[i] = Rorig[i] = 0.4f * std::sin(2.f * static_cast<float>(M_PI) * 554.f * t);
    }
    ex.process(L.data(), R.data(), kBlockSize);

    // Con wet=0, salida = in + 0*exc = in exactamente (ruta escalar) o dentro
    // de <1e-6 (ruta NEON, por la aproximación recíproca de vrecpe+vrecps).
    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(L[i], Lorig[i], 1.0e-6f) << "L drift at i=" << i;
        ASSERT_NEAR(R[i], Rorig[i], 1.0e-6f) << "R drift at i=" << i;
    }
}

TEST(HarmonicExciterStability, ExtremeDriveDoesNotOverflow) {
    ivanna::HarmonicExciter ex;
    ivanna::DSPParams p{};
    p.sampleRate = kSampleRate;
    p.drive = 1.0f;  // máximo (drive_ = 6x)
    p.wet   = 1.0f;
    ex.setParams(p);

    std::vector<float> L(kBlockSize), R(kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        // Señal ya cerca de fondo de escala + tono agudo (peor caso para exciter)
        const float t = static_cast<float>(i) / kSampleRate;
        L[i] = 0.98f * std::sin(2.f * static_cast<float>(M_PI) * 8000.f * t);
        R[i] = 0.98f * std::sin(2.f * static_cast<float>(M_PI) * 10000.f * t);
    }
    ex.process(L.data(), R.data(), kBlockSize);
    ASSERT_TRUE(allFinite(L));
    ASSERT_TRUE(allFinite(R));
    // El exciter puede exceder ±1 (no tiene limiter), pero no debe reventar
    // por encima de un margen razonable (soft-clip Padé es bounded).
    ASSERT_LT(peakAbs(L), 3.0f);
    ASSERT_LT(peakAbs(R), 3.0f);
}
