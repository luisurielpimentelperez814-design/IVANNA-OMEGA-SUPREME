#pragma once
#include "dsp_types.h"

namespace ivanna {

class StereoWidener {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();  // FIX: Added missing reset() method

private:
    float width_     = 1.f;
    float halfWidth_ = 0.5f;  // width * 0.5, precomputado
};

} // namespace ivanna
