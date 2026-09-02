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
    float width_ = 1.0f;        // objetivo (slider)
    float widthNow_ = 1.0f;     // valor suavizado aplicado por muestra
    float widthSmooth_ = 0.9995f;  // ~15ms @ 96kHz, recalculado en setParams()
    // FIX: halfWidth_ era [[maybe_unused]] — calculado como 0.5*width_ en
    // setParams() pero nunca leído en process(). Eliminado: sin cambio de
    // comportamiento. El proceso M/S usa widthNow_ directamente.
    // FIX (tuning magistral): crossover mono-safe de graves — sin esto, un
    // widener M/S puro cancela fase en mono por debajo de ~150Hz (bug real
    // de todo widener naive). El "side" se separa en low/high; el ensanche
    // sólo se aplica por encima del corte, y por debajo se limita el boost
    // de forma proporcional a cuánto se está ensanchando (ver .cpp).
    Biquad sideLpf_;
    uint32_t lastSampleRate_ = 96000;

    // DC blockers en canales L y R — HPF primer orden ~5Hz
    float dcxL_ = 0.f, dcyL_ = 0.f;
    float dcxR_ = 0.f, dcyR_ = 0.f;
    float dcCoef_ = 0.99985f; // fc≈5Hz @ 96kHz
};
}
