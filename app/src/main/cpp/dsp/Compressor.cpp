#include "../include/Compressor.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

Compressor::Compressor() {
    reset();
}

void Compressor::setParams(const DSPParams& p) {
    sr_ = static_cast<float>(p.sampleRate);
    threshold_ = -24.0f + p.alpha * 24.0f;
    ratio_ = 1.0f + p.beta * 19.0f;
    float atMs = 5.0f + (1.0f - p.gamma) * 95.0f;
    float relMs = 50.0f + (1.0f - p.gamma) * 450.0f;
    attackCoef_ = std::exp(-1.0f / (sr_ * atMs * 0.001f));
    releaseCoef_ = std::exp(-1.0f / (sr_ * relMs * 0.001f));
    float reduction = threshold_ * (1.0f - 1.0f / ratio_);
    makeupGain_ = std::pow(10.0f, (-reduction * 0.5f) / 20.0f);
    inv_atk_ = 1.0f - attackCoef_;
    inv_rel_ = 1.0f - releaseCoef_;
    slope_ = 1.0f - 1.0f / ratio_;
}

void Compressor::setThreshold(float db) {
    threshold_ = db;
}

void Compressor::setRatio(float ratio) {
    ratio_ = ratio;
    slope_ = 1.0f - 1.0f / ratio_;
}

void Compressor::setAttack(float ms) {
    attackCoef_ = std::exp(-1.0f / (sr_ * ms * 0.001f));
    inv_atk_ = 1.0f - attackCoef_;
}

void Compressor::setRelease(float ms) {
    releaseCoef_ = std::exp(-1.0f / (sr_ * ms * 0.001f));
    inv_rel_ = 1.0f - releaseCoef_;
}

void Compressor::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;

    constexpr float k20DivLn10 = 8.6858896381f;
    constexpr float kLn10Div20 = 0.11512925465f;

    const float attackCoef = attackCoef_;
    const float releaseCoef = releaseCoef_;
    const float threshold = threshold_;
    const float ratioInv = 1.0f - 1.0f / ratio_;
    const float makeup = makeupGain_;
    float env = env_;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for (int i = 0; i < frames; ++i) {
        float peak = std::fmax(std::fabs(left[i]), std::fabs(right[i]));
        if (peak < 1e-6f) peak = 1e-6f;

        float coef = (peak > env)? attackCoef : releaseCoef;
        env = coef * env + (1.0f - coef) * peak;

        float envDb = k20DivLn10 * std::log(env);
        float gainDb = 0.0f;
        if (envDb > threshold) {
            gainDb = (threshold - envDb) * ratioInv;
        }

        float lin = makeup * std::exp(gainDb * kLn10Div20);
        left[i] *= lin;
        right[i] *= lin;
    }
#pragma clang diagnostic pop

    env_ = env;
}

void Compressor::reset() {
    env_ = 0.0f;
}

} // namespace ivanna
