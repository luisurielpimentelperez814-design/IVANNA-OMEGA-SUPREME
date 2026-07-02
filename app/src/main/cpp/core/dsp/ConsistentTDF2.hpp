#pragma once
#include <arm_neon.h>

struct VectorCoefficients {
    float32x4_t b0, b1, b2, a1, a2;
};

class ConsistentTDF2 {
private:
    float32x4_t s1 = vdupq_n_f32(0.0f);
    float32x4_t s2 = vdupq_n_f32(0.0f);

public:
    inline float32x4_t process(const float32x4_t in, const VectorCoefficients& coef) noexcept {
        float32x4_t out = vmlaq_f32(s1, coef.b0, in);
        s1 = vmlaq_f32(s2, coef.b1, in);
        s1 = vmlsq_f32(s1, coef.a1, out);
        s2 = vmulq_f32(coef.b2, in);
        s2 = vmlsq_f32(s2, coef.a2, out);
        return out;
    }

    // Proyección de estado para evitar el "pop" al cambiar EQ
    inline void project_state(const VectorCoefficients& old_c, const VectorCoefficients& new_c) noexcept {
        float32x4_t dc_old = vaddq_f32(old_c.b0, vaddq_f32(old_c.b1, old_c.b2));
        float32x4_t dc_new = vaddq_f32(new_c.b0, vaddq_f32(new_c.b1, new_c.b2));
        
        // Evitamos división por cero simple
        float32x4_t scale = vdivq_f32(dc_new, dc_old);
        s1 = vmulq_f32(s1, scale);
        s2 = vmulq_f32(s2, scale);
    }
};
