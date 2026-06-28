#include "../include/GainStage.h"
#include <cmath>

namespace ivanna {

// Aproximación rápida de dB a lineal usando exp2f (más rápido que pow)
// 10^(x/20) = 2^(x * log2(10)/20) ≈ 2^(x * 0.166096404f)
static inline __attribute__((always_inline)) float dbToLin(float db) {
    return std::exp2f(db * 0.1660964f);
}

void GainStage::setParams(const DSPParams& p) {
    sr_ = p.sampleRate;
    // Coeficiente de suavizado precalculado (15 ms)
    smoothCoeff_      = std::exp(-1.f / (sr_ * 0.015f));
    oneMinusSmooth_   = 1.f - smoothCoeff_;
    
    inputGain_  = dbToLin((p.mix - 0.5f) * 12.f);
    outputGain_ = dbToLin(p.master);
    
    // Reiniciar estado al cambiar parámetros
    currentIn_  = inputGain_;
    currentOut_ = outputGain_;
}

__attribute__((hot, flatten))
void GainStage::processInput(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    
    const float smooth = smoothCoeff_;
    const float one_minus_smooth = oneMinusSmooth_;
    const float target = inputGain_;
    float current = currentIn_;
    
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < frames; ++i) {
        current = smooth * current + one_minus_smooth * target;
        left[i]  *= current;
        right[i] *= current;
    }
    
    currentIn_ = current;
}

__attribute__((hot, flatten))
void GainStage::processOutput(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    
    const float smooth = smoothCoeff_;
    const float one_minus_smooth = oneMinusSmooth_;
    const float target = outputGain_;
    float current = currentOut_;
    
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < frames; ++i) {
        current = smooth * current + one_minus_smooth * target;
        left[i]  *= current;
        right[i] *= current;
    }
    
    currentOut_ = current;
}

void GainStage::reset() {
    currentIn_  = inputGain_;
    currentOut_ = outputGain_;
}

} // namespace ivanna
