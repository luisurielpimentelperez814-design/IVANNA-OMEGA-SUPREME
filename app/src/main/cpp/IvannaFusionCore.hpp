#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif


namespace Ivanna {

constexpr size_t BLOCK_SIZE = 128;
constexpr size_t FIR_TAPS = 256;
constexpr size_t BANDS_512 = 512;

struct AudioBuffer {
    alignas(16) float left[BLOCK_SIZE];
    alignas(16) float right[BLOCK_SIZE];
};

inline float fast_tanh_scalar(float x) {
    float x2 = x * x;
    float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
}

// FIX (distorsion armonica constante): tanh() NO es transparente — deforma
// toda muestra con |x| > ~0.3 (THD de ~2% a 0.5 de amplitud) y se aplicaba a
// TODO el audio al final del FusionCore. Soft-knee C1: identidad exacta hasta
// 0.8, curva racional suave por encima con asintota en 1.0 (pendiente y valor
// continuos en el codo). Solo actua sobre picos reales.
inline float soft_knee_scalar(float x) {
    constexpr float kKnee = 0.8f;
    constexpr float kRange = 1.0f - kKnee;
    const float ax = x < 0.0f ? -x : x;
    if (ax <= kKnee) return x;
    const float over = (ax - kKnee) * (1.0f / kRange);
    const float y = kKnee + kRange * (over / (1.0f + over));
    return x < 0.0f ? -y : y;
}

} // namespace Ivanna
