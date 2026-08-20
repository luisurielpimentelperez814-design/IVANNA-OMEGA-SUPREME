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
    // Transparencia absoluta: una banda a 0 dB es matematicamente la
    // identidad, pero procesarla igual acumula ruido de cuantizacion float
    // (8 biquads en cascada => ~8x el ruido de redondeo) y gasta CPU.
    // active_[b] = false => la banda se salta bit-exacto.
    bool active_[NUM_BANDS] = {false,false,false,false,false,false,false,false};
    float sampleRate_ = 96000.0f;
};
}
