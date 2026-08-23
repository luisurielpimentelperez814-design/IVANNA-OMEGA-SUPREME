#if defined(__clang__)
#pragma clang optimize on
#else
#pragma GCC optimize("O3", "unroll-loops")
#endif
#include "../include/HarmonicExciter.h"
#include <cmath>
#include <cstring>

namespace ivanna {

static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    // PARCHE ANTI-GRANO: Reemplazo del clamp estricto por una atenuación 
    // asintótica suave para evitar discontinuidades de pendiente en la saturación.
    float absX = x < 0.0f ? -x : x;
    if (absX > 2.5f) {
        x = x > 0.0f ? (2.5f + 0.5f * std::tanh((x - 2.5f) * 0.5f)) : (-2.5f - 0.5f * std::tanh((-x - 2.5f) * 0.5f));
    }
    float x2 = x * x;
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

static constexpr float kExcCeiling = 0.98855f;
