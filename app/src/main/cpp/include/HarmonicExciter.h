#pragma once
#include "dsp_types.h"

namespace ivanna {

// Harmonic exciter: drive → soft-clip saturation + 2nd/3rd harmonic generation
// CON ANTI-ALIASING: Oversampling 2x + LPF post-clip
class HarmonicExciter {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

private:
    float drive_ = 1.f;
    float wet_   = 0.5f;
    float dry_   = 0.5f;
    
    // HPF to feed only highs into exciter (3 kHz cutoff)
    Biquad hpfL_, hpfR_;
    
    // Anti-aliasing: oversampling 2x buffers
    static constexpr int OS_FACTOR = 2;  // 2x oversampling
    static constexpr int MAX_OS_FRAMES = 4096;
    float osLeft_[MAX_OS_FRAMES * OS_FACTOR];   // Buffer para oversampling
    float osRight_[MAX_OS_FRAMES * OS_FACTOR];
    
    // Resampling interpolation filter (LPF para downsample)
    Biquad osLpfL_, osLpfR_;  // 11.5 kHz LPF @ 96kHz (anti-aliasing en downsample)
    
    // Interpolación lineal para upsample
    float lastL_ = 0.f, lastR_ = 0.f;
};

} // namespace ivanna
