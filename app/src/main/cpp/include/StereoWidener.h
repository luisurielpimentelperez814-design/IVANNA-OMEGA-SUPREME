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
    // FIX (tuning magistral): crossover mono-safe de graves — sin esto, un
    // widener M/S puro cancela fase en mono por debajo de ~150Hz (bug real
    // de todo widener naive). El "side" se separa en low/high; el ensanche
    // sólo se aplica por encima del corte, y por debajo se limita el boost
    // de forma proporcional a cuánto se está ensanchando (ver .cpp).
    Biquad sideLpf_;
    uint32_t lastSampleRate_ = 48000;
};
}
