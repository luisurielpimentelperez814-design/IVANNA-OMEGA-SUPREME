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
    const float s = smoothCoeff_, o = oneMinusSmooth_, t = outputGain_;
    float c = currentOut_;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for (int i = 0; i < frames; ++i) {
        c = s * c + o * t;
        left[i] *= c;
        right[i] *= c;
    }
#pragma clang diagnostic pop
    currentOut_ = c;
}

void GainStage::reset() {
    currentIn_ = inputGain_;
    currentOut_ = outputGain_;
}

} // namespace ivanna
