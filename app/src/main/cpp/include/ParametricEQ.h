#pragma once
#include "dsp_types.h"

namespace ivanna {
class ParametricEQ {
public:
    ParametricEQ() noexcept;
    void reset() noexcept;
    void setSampleRate(float sr) noexcept;
    void setBand(int band, float freq, float q, float gainDb) noexcept;
    void setParams(const DSPParams& p) noexcept;
    void process(float* left, float* right, int frames) noexcept;
private:
    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
        inline float processSample(float x) noexcept {
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y; return y;
        }
        void reset() noexcept { x1=x2=y1=y2=0; }
    };
    static constexpr int NUM_BANDS = 8;
    Biquad bandsL[NUM_BANDS];
    Biquad bandsR[NUM_BANDS];
    float sampleRate_ = 48000.0f;
};
}
