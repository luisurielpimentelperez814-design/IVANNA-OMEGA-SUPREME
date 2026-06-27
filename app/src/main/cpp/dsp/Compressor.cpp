#include "../include/Compressor.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

void Compressor::setParams(const DSPParams& p) {
    sr_ = p.sampleRate;
    // alpha → threshold  -24..0 dB
    threshold_   = -24.f + p.alpha * 24.f;
    // beta → ratio 1..20
    ratio_       = 1.f + p.beta * 19.f;
    // gamma → attack/release balance: 0=slow, 1=fast
    float atMs   = 5.f   + (1.f - p.gamma) * 95.f;  // 5..100 ms
    float relMs  = 50.f  + (1.f - p.gamma) * 450.f; // 50..500 ms
    attackCoef_  = std::exp(-1.f / (sr_ * atMs  * 0.001f));
    releaseCoef_ = std::exp(-1.f / (sr_ * relMs * 0.001f));
    // Simple makeup gain estimation
    float reduction = threshold_ * (1.f - 1.f/ratio_);
    makeupGain_ = std::pow(10.f, (-reduction * 0.5f) / 20.f);
}

void Compressor::process(float* left, float* right, int frames) {
    // FIX #6: renombrado LOG2DB → kLn2dB.
    // El valor 20/ln(10) convierte logaritmo NATURAL a dB.
    // "LOG2DB" sugería log-base-2, lo que era engañoso y propenso a errores.
    constexpr float kLn2dB  = 8.6858896381f; // 20/ln(10)
    constexpr float kDB2Lin = 0.11512925465f; // ln(10)/20

    for (int i = 0; i < frames; ++i) {
        float peak = std::max(std::fabs(left[i]), std::fabs(right[i]));
        if (peak < 1e-6f) peak = 1e-6f;

        // Level detector — peak envelope follower
        if (peak > env_)
            env_ = attackCoef_  * env_ + (1.f - attackCoef_)  * peak;
        else
            env_ = releaseCoef_ * env_ + (1.f - releaseCoef_) * peak;

        float envDb = kLn2dB * std::log(env_);
        float gainDb = 0.f;
        if (envDb > threshold_)
            gainDb = (threshold_ - envDb) * (1.f - 1.f/ratio_);

        float lin = makeupGain_ * std::exp(gainDb * kDB2Lin);
        left[i]  *= lin;
        right[i] *= lin;
    }
}

void Compressor::reset() { env_ = 0.f; }

} // namespace ivanna
