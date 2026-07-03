#include "../include/StereoWidener.h"

namespace ivanna {

void StereoWidener::setParams(const DSPParams& p) {
    // FIX audio-cleanup: cap width 2.0->1.5 (evita hiss/artefactos de fase en Side)
    width_ = p.gamma * 1.5f;
}

// Setter directo, independiente de DSPParams::gamma. Declarado en el header
// pero sin cuerpo (FIX: completar API — permite ajustar width sin pasar por
// setParams cuando solo ese parámetro cambia, p.ej. desde un control de UI
// dedicado o el preset Anti-Dolby que reduce widener 30% en modo Speech).
void StereoWidener::setWidth(float w) {
    // FIX audio-cleanup: cap width 1.5
    width_ = w < 0.f ? 0.f : (w > 1.5f ? 1.5f : w);
}

__attribute__((hot, flatten))
void StereoWidener::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    
    const float w = width_;
    // Coeficientes precalculados para Mid/Side directo:
    // left_out  = 0.5*(1+w)*L + 0.5*(1-w)*R
    // right_out = 0.5*(1-w)*L + 0.5*(1+w)*R
    const float c_plus  = 0.5f * (1.f + w);
    const float c_minus = 0.5f * (1.f - w);
    
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < frames; ++i) {
        float l = left[i];
        float r = right[i];
        left[i]  = c_plus * l + c_minus * r;
        right[i] = c_minus * l + c_plus * r;
    }
}

void StereoWidener::reset() {
    // No persistent state to reset (width_ is set via setParams)
}

} // namespace ivanna
