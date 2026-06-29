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

private:
    float sr_ = 48000.0f;
    float threshold_ = -12.0f;
    float ratio_ = 4.0f;
    float attackCoef_ = 0.99f;
    float releaseCoef_ = 0.999f;
    float makeupGain_ = 1.0f;
    float env_ = 0.0f;

    [[maybe_unused]] float inv_atk_ = 1.0f;
    [[maybe_unused]] float inv_rel_ = 1.0f;
    [[maybe_unused]] float slope_ = 0.75f;
};

} // namespace ivanna
