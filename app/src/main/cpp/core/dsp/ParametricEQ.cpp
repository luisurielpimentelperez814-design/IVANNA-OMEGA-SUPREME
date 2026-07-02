#include "../include/ParametricEQ.h"
#include <cmath>
#include <arm_neon.h>

namespace ivanna {

ParametricEQ::ParametricEQ() noexcept { reset(); }

void ParametricEQ::reset() noexcept { 
    // Los filtros ConsistentTDF2 se reinician automáticamente 
    // al ser destruidos/recreados, pero aquí aseguramos el estado inicial.
}

void ParametricEQ::setSampleRate(float sr) noexcept { sampleRate_ = sr; }

void ParametricEQ::setBand(int b, float f, float q, float g) noexcept {
    if(b < 0 || b >= NUM_BANDS) return;

    // Cálculos de coeficientes (matemática original mantenida)
    float A = powf(10.0f, g / 40.0f);
    float w0 = 2.0f * float(M_PI) * f / sampleRate_;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / (2.0f * q);
    
    float b0 = 1.0f + alpha * A, b1 = -2.0f * c, b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A, a1 = -2.0f * c, a2 = 1.0f - alpha / A;
    
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    VectorCoefficients new_c = {
        vdupq_n_f32(b0), vdupq_n_f32(b1), vdupq_n_f32(b2),
        vdupq_n_f32(a1), vdupq_n_f32(a2)
    };

    // PROYECCIÓN DE ESTADO: Vital para evitar clics al ajustar EQ en tiempo real
    bandsL[b].project_state(coefsL[b], new_c);
    bandsR[b].project_state(coefsR[b], new_c);

    coefsL[b] = new_c;
    coefsR[b] = new_c;
}

void ParametricEQ::setParams(const DSPParams& p) noexcept {
    auto clampDb = [](float db) { return db < -18.f ? -18.f : db > 18.f ? 18.f : db; };
    setBand(0, 80.f, 0.707f, clampDb(p.low));
    setBand(1, 200.f, p.resonance, clampDb(p.low * 0.5f));
    setBand(2, 500.f, p.resonance, 0.f);
    setBand(3, p.freq, p.resonance, clampDb(p.master * 0.25f));
    setBand(4, 2500.f, p.resonance, clampDb(p.mid));
    setBand(5, 5000.f, p.resonance, clampDb(p.high));
    setBand(6, 8000.f, p.resonance, clampDb(p.presence));
    setBand(7, 12000.f, 0.707f, clampDb(p.high * 0.5f));
}

// PROCESAMIENTO VECTORIAL (SIMD)
void ParametricEQ::process(float* l, float* r, int frames) noexcept {
    if (frames <= 0) return;

    // Procesamos bloques de 4 muestras a la vez
    for (int i = 0; i < frames; i += 4) {
        float32x4_t vL = vld1q_f32(&l[i]);
        float32x4_t vR = vld1q_f32(&r[i]);

        // Aplicamos el filtro a las 4 muestras simultáneamente por cada banda
        for (int b = 0; b < NUM_BANDS; ++b) {
            vL = bandsL[b].process(vL, coefsL[b]);
            vR = bandsR[b].process(vR, coefsR[b]);
        }

        vst1q_f32(&l[i], vL);
        vst1q_f32(&r[i], vR);
    }
}

} // namespace ivanna
