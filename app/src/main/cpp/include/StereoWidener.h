#pragma once
#include "dsp_types.h"

namespace ivanna {
class StereoWidener {
public:
    void setParams(const DSPParams& p);
    void setWidth(float w);
    void process(float* l, float* r, int frames);
    void reset();
private:
    float width_ = 1.0f;
    [[maybe_unused]] float halfWidth_ = 0.5f;
};
}
