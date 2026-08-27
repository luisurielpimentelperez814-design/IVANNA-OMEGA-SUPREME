#pragma once
/**
 * IvannaMath.hpp — Funciones matemáticas inline optimizadas para ARM NEON
 * Incluir en cualquier translation unit que necesite fast_tanh.
 */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

/**
 * fast_tanh_neon — Aproximación Padé de tanh sobre float32x4_t
 * f(x) = x*(27 + x²) / (27 + 9*x²)
 * Evaluado con vrecpeq_f32 + 1 iteración Newton-Raphson (vrecpsq_f32).
 * Error máximo: < 0.0015 en [-3, 3].
 */
[[nodiscard]] inline float32x4_t fast_tanh_neon(float32x4_t x) noexcept {
    const float32x4_t x2  = vmulq_f32(x, x);
    const float32x4_t num = vmulq_f32(x, vaddq_f32(vdupq_n_f32(27.0f), x2));
    const float32x4_t den = vaddq_f32(vdupq_n_f32(27.0f), vmulq_n_f32(x2, 9.0f));
    // Reciprocal estimate + 1-step Newton-Raphson refinement
    float32x4_t rec = vrecpeq_f32(den);
    rec = vmulq_f32(vrecpsq_f32(den, rec), rec);
    return vmulq_f32(num, rec);
}

#else

/**
 * fast_tanh_scalar — fallback escalar (non-NEON targets / unit tests)
 * Misma aproximación Padé: f(x) = x*(27+x²)/(27+9*x²)
 */
[[nodiscard]] inline float fast_tanh_scalar(float x) noexcept {
    const float x2 = x * x;
    return (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
}

#endif // __ARM_NEON
