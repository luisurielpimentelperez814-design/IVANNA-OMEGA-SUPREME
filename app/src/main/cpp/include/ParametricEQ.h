#pragma once
#include "dsp_types.h"

namespace ivanna {

// 3-band parametric EQ: low shelf + mid peak + high shelf + presence peak
class ParametricEQ {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

private:
    Biquad lowL, lowR;
    Biquad midL, midR;
    Biquad highL, highR;
    Biquad presL, presR;
    DSPParams last_;
};

} // namespace ivanna
