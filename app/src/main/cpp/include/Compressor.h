#pragma once
#include "dsp_types.h"

namespace ivanna {

class Compressor {
public:
    Compressor();

    void setParams(const DSPParams& p);
    void setThreshold(float db);
    void setRatio(float ratio);
    void setAttack(float ms);
    void setRelease(float ms);
    void process(float* left, float* right, int frames);
    void reset();

    void setRuntimeAmount(float amount01) noexcept {
        runtimeAmount_ =
            amount01 < 0.f ? 0.f :
            (amount01 > 1.f ? 1.f : amount01);
    }

private:
    void recomputeMakeup() noexcept;

    float sr_ = 96000.0f;
    float threshold_ = -12.0f;
    float ratio_ = 4.0f;
    float attackCoef_ = 0.99f;
    float releaseCoef_ = 0.999f;

    float makeupGain_ = 1.0f;   // lineal (solo informativo/compat)
    float makeupDb_ = 0.0f;     // techo de compensacion en dB
    float env_ = 0.0f;
    float runtimeAmount_ = 0.0f;

    Biquad scHpfL_;
    Biquad scHpfR_;

    [[maybe_unused]] float inv_atk_ = 1.0f;
    [[maybe_unused]] float inv_rel_ = 1.0f;
    [[maybe_unused]] float slope_ = 0.75f;
};

}
