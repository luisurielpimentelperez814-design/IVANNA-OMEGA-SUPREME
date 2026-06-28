#include "../include/Compressor.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

void Compressor::setParams(const DSPParams& p) {
    sr_ = p.sampleRate;
    threshold_   = -24.f + p.alpha * 24.f;
    ratio_       = 1.f + p.beta * 19.f;
    float atMs   = 5.f   + (1.f - p.gamma) * 95.f;
    float relMs  = 50.f  + (1.f - p.gamma) * 450.f;
    attackCoef_  = std::exp(-1.f / (sr_ * atMs  * 0.001f));
    releaseCoef_ = std::exp(-1.f / (sr_ * relMs * 0.001f));
    float reduction = threshold_ * (1.f - 1.f/ratio_);
    makeupGain_ = std::pow(10.f, (-reduction * 0.5f) / 20.f);
}

__attribute__((hot, flatten))
void Compressor::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    
    // Constantes para conversión dB <-> lineal (single‑precision)
    constexpr float k20DivLn10 = 8.6858896381f;   // 20 / ln(10)
    constexpr float kLn10Div20 = 0.11512925465f;  // ln(10) / 20
    
    // Cache de coeficientes en registros (evita lecturas de memoria)
    const float attackCoef  = attackCoef_;
    const float releaseCoef = releaseCoef_;
    const float threshold   = threshold_;
    const float ratioInv    = 1.f - 1.f / ratio_;  // pendiente de compresión
    const float makeup      = makeupGain_;
    float env               = env_;
    
    for (int i = 0; i < frames; ++i) {
        // Detector de nivel – branchless con fmaxf
        float peak = std::fmaxf(std::fabs(left[i]), std::fabs(right[i]));
        if (peak < 1e-6f) peak = 1e-6f;
        
        // Envelope follower branchless
        float coef = (peak > env) ? attackCoef : releaseCoef;
        env = coef * env + (1.f - coef) * peak;
        
        // Conversión a dB con logf (precisión float, más rápido que double)
        float envDb = k20DivLn10 * std::logf(env);
        float gainDb = 0.f;
        if (envDb > threshold) {
            gainDb = (threshold - envDb) * ratioInv;
        }
        
        // Ganancia final (expf en lugar de exp para precisión float)
        float lin = makeup * std::expf(gainDb * kLn10Div20);
        left[i]  *= lin;
        right[i] *= lin;
    }
    
    env_ = env;
}

void Compressor::reset() { env_ = 0.f; }

} // namespace ivanna
