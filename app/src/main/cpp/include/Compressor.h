#pragma once
#include "dsp_types.h"

namespace ivanna {

class Compressor {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

private:
    float threshold_   = -12.f;
    float ratio_       = 4.f;
    float attackCoef_  = 0.f;
    float releaseCoef_ = 0.f;
    float makeupGain_  = 1.f;
    float env_         = 0.f;
    // Precomputados para evitar resta en el hot loop
    float inv_atk_     = 1.f;  // 1 - attackCoef_
    float inv_rel_     = 1.f;  // 1 - releaseCoef_
    float slope_       = 0.75f; // 1 - 1/ratio_
    uint32_t sr_       = 48000;
};

} // namespace ivanna
