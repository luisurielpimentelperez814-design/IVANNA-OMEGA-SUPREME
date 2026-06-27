#pragma once
#include "dsp_types.h"

namespace ivanna {

// Harmonic exciter: drive → soft-clip saturation + 2nd/3rd harmonic generation
// Wet/dry blend controlled by drive + wet params
class HarmonicExciter {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

private:
    float drive_ = 1.f;
    float wet_   = 0.5f;
    float dry_   = 0.5f;
    // HPF to feed only highs into exciter
    Biquad hpfL_, hpfR_;
};

} // namespace ivanna
