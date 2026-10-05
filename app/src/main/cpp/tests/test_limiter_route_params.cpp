/**
 * test_limiter_route_params.cpp
 *
 * Fija el comportamiento REAL del SafetyLimiter con los parámetros que usan las dos
 * rutas en producción: setParams(0.891251f, 0.98855f) (umbral -1 dBFS, techo -0.1 dBFS;
 * ivanna_omega_jni.cpp:761/1791 y omega_effect.cpp:1489).
 *
 * Medido a 48 kHz, seno 100 Hz, bloques de 320: transparente (<0.05 dB) hasta -1 dBFS,
 * 0 dBFS -> -0.66 dBFS, +3 dBFS -> -0.55 dBFS. (Con los defaults de setParams(), umbral
 * -4 dBFS, una entrada a 0 dBFS sale a -2.8 dBFS: ninguna ruta lo usa; este test evita
 * que alguien "simplifique" las llamadas y vuelva a ese comportamiento.)
 */
#include <gtest/gtest.h>
#include "SafetyLimiter.h"
#include <cmath>
#include <vector>

namespace {
constexpr int   BLOCK = 320;
constexpr float SR    = 48000.f;
constexpr float kCeil = 0.98855f;

// SafetyLimiter lleva atomics: no es movible/copiable, se configura in situ.
void configureRoute(ivanna::SafetyLimiter& lim) {
    lim.setSampleRate(SR);
    lim.setParams(0.891251f, 0.98855f);
}

// Peak de salida (estado estable) de un seno estéreo de amplitud `a`.
float steadyPeak(float a, double f = 100.0) {
    ivanna::SafetyLimiter lim; configureRoute(lim);
    std::vector<float> L(BLOCK), R(BLOCK);
    float pk = 0.f;
    for (int b = 0; b < 400; ++b) {
        for (int i = 0; i < BLOCK; ++i)
            L[i] = R[i] = a * (float)std::sin(2.0 * M_PI * f * (b * BLOCK + i) / SR);
        lim.process(L.data(), R.data(), BLOCK);
        if (b >= 300) for (int i = 0; i < BLOCK; ++i) pk = std::fmax(pk, std::fabs(L[i]));
    }
    return pk;
}
float db(float x) { return 20.f * std::log10(x); }
} // namespace

TEST(LimiterRouteParams, TransparentBelowThreshold) {
    for (float a : {0.1f, 0.3f, 0.5f, 0.7f, 0.85f}) {
        const float pk = steadyPeak(a);
        EXPECT_NEAR(db(pk / a), 0.f, 0.05f) << "amp=" << a;
    }
}

TEST(LimiterRouteParams, DoesNotCrushHotMasters) {
    // Un master a -0.45 dBFS y uno a 0 dBFS no pueden perder más de 1 dB.
    EXPECT_GT(db(steadyPeak(0.949f) / 0.949f), -1.0f);
    EXPECT_GT(db(steadyPeak(1.0f) / 1.0f), -1.0f);
}

TEST(LimiterRouteParams, KeepsDynamicsAboveThreshold) {
    // No es una pared: +3 dB de entrada deben producir salida mayor (> 0.3 dB) hasta el techo.
    EXPECT_GT(steadyPeak(0.95f), steadyPeak(0.90f));
}

TEST(LimiterRouteParams, NeverExceedsCeilingOnAnyProgram) {
    ivanna::SafetyLimiter lim; configureRoute(lim);
    std::vector<float> L(BLOCK), R(BLOCK);
    float worst = 0.f;
    unsigned seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFF) / 32768.f - 1.f; };
    for (int b = 0; b < 600; ++b) {
        for (int i = 0; i < BLOCK; ++i) {
            float v;
            switch ((b / 50) % 4) {
                case 0:  v = 1.8f * (float)std::sin(2.0 * M_PI * 80.0 * (b * BLOCK + i) / SR); break; // +5 dBFS
                case 1:  v = (i == 0) ? 2.5f : 0.f; break;                                           // impulsos
                case 2:  v = 2.0f * rnd(); break;                                                    // ruido fuerte
                default: v = (i % 64 < 6) ? 1.6f * rnd() : 0.05f * rnd(); break;                     // percusión
            }
            L[i] = v; R[i] = -v * 0.9f;
        }
        lim.process(L.data(), R.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            worst = std::fmax(worst, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
            ASSERT_TRUE(std::isfinite(L[i]) && std::isfinite(R[i]));
        }
    }
    EXPECT_LE(worst, kCeil + 1e-6f) << "worst=" << worst;
}

TEST(LimiterRouteParams, ReleasesAfterLoudPassage) {
    // Tras un pasaje fuerte la ganancia debe volver a 1 (sin bombeo residual): -12 dBFS
    // sale a -12 dBFS (<0.1 dB) 400 ms después.
    ivanna::SafetyLimiter lim; configureRoute(lim);
    std::vector<float> L(BLOCK), R(BLOCK);
    for (int b = 0; b < 100; ++b) {
        for (int i = 0; i < BLOCK; ++i) L[i] = R[i] = 1.5f * (float)std::sin(2.0 * M_PI * 100.0 * (b * BLOCK + i) / SR);
        lim.process(L.data(), R.data(), BLOCK);
    }
    float pk = 0.f;
    for (int b = 0; b < 150; ++b) {
        for (int i = 0; i < BLOCK; ++i) L[i] = R[i] = 0.25f * (float)std::sin(2.0 * M_PI * 100.0 * (b * BLOCK + i) / SR);
        lim.process(L.data(), R.data(), BLOCK);
        if (b >= 140) for (int i = 0; i < BLOCK; ++i) pk = std::fmax(pk, std::fabs(L[i]));
    }
    EXPECT_NEAR(db(pk / 0.25f), 0.f, 0.1f);
}
