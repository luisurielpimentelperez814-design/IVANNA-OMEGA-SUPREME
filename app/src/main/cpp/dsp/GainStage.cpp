#include "../include/GainStage.h"
#include "../include/dsp_types.h"
#include <cmath>

namespace ivanna {

static inline float dbToLin(float db) {
    return std::exp2f(db * 0.1660964f);
}

void GainStage::setParams(const DSPParams& p) {
    sr_ = static_cast<float>(p.sampleRate);
    smoothCoeff_ = std::exp(-1.0f / (sr_ * 0.015f));
    oneMinusSmooth_ = 1.0f - smoothCoeff_;
    inputGain_ = dbToLin((p.mix - 0.5f) * 12.0f);
    outputGain_ = dbToLin(p.master);
    currentIn_ = inputGain_;
    currentOut_ = outputGain_;
    // Limitador: release ~50ms
    limRelCoef_ = std::exp(-1.0f / (sr_ * 0.050f));
}

void GainStage::processInput(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    const float s = smoothCoeff_, o = oneMinusSmooth_, t = inputGain_;
    float c = currentIn_;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for (int i = 0; i < frames; ++i) {
        c = s * c + o * t;
        left[i] *= c;
        right[i] *= c;
    }
#pragma clang diagnostic pop
    currentIn_ = c;
}

void GainStage::processOutput(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;
    const float s = smoothCoeff_, o = oneMinusSmooth_, t = outputGain_ * runtimeMul_;
    float c = currentOut_;
    float lg = limGain_;
    const float rel = limRelCoef_;
    const float thr = LIM_THRESHOLD;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for (int i = 0; i < frames; ++i) {
        c = s * c + o * t;
        float l = left[i]  * c;
        float r = right[i] * c;

        // Limitador true-peak: ataque instantáneo (1 muestra), release ~50ms
        float peak = l > 0.f ? l : -l;
        float rp   = r > 0.f ? r : -r;
        if (rp > peak) peak = rp;
        if (peak > 1e-9f) {
            float needed = thr / peak;
            if (needed < lg) {
                lg = needed;          // ataque: 1 muestra — brick-wall
            } else {
                lg = rel * lg + (1.f - rel) * 1.0f;  // release suave
                if (lg > 1.f) lg = 1.f;
            }
        } else {
            lg = rel * lg + (1.f - rel) * 1.0f;
            if (lg > 1.f) lg = 1.f;
        }

        left[i]  = l * lg;
        right[i] = r * lg;
    }
#pragma clang diagnostic pop
    currentOut_ = c;
    limGain_    = lg;
}

void GainStage::reset() {
    currentIn_ = inputGain_;
    currentOut_ = outputGain_;
    limGain_ = 1.0f;
}

} // namespace ivanna
