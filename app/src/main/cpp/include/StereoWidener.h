#pragma once
#include "dsp_types.h"

namespace ivanna {

// M/S stereo widener — gamma controls width 0..2 (1=original)
class StereoWidener {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);

private:
    float width_ = 1.f;
};

} // namespace ivanna
