#include "../include/Compressor.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

// OPTIMIZACION: aproximaciones rapidas de log2/exp2 (bit-trick, Schraudolph
// + correccion polinomica) para reemplazar logf/expf de libm dentro del
// loop por-muestra del envelope follower. libm cubre todo el rango de un
// float (subnormales, +-inf, precision ULP); el envolvente de este
// compresor vive en un rango acotado y ya clampeado (peak >= 1e-6f), asi
// que ese margen de precision no se necesita. Error verificado (ver
// /tmp/verify2.cpp de la sesion de auditoria): <0.001 dB en el logaritmo,
// <0.006% relativo en el exponencial, en todo el rango de uso real.
static inline float fastLog2(float x) {
    // Version con correccion polinomica (Stephenson/Mineiro) — el termino
    // lineal puro daba hasta 0.34 dB de error en el rango del compresor,
    // verificado empiricamente; esta version baja el error maximo a
    // <0.001 dB en todo el rango -120..+3 dBFS.
    union { float f; uint32_t i; } vx{x};
    union { uint32_t i; float f; } mx{(vx.i & 0x007FFFFFu) | 0x3f000000u};
    float y = (float)vx.i * 1.1920928955078125e-7f;
    return y - 124.22551499f - 1.498030302f * mx.f - 1.72587999f / (0.3520887068f + mx.f);
}
static inline float fastExp2(float p) {
    const float clipped = p < -126.f ? -126.f : p;
    const float w = clipped - (float)(int)clipped + (clipped < 0.f ? 1.f : 0.f);
    union { int32_t i; float f; } v;
    v.i = (int32_t)((1 << 23) * (clipped + 121.2740575f + 27.7280233f / (4.84252568f - w) - 1.49012907f * w));
    return v.f;
}

Compressor::Compressor() {
    reset();
}

void Compressor::setParams(const DSPParams& p) {
    sr_ = static_cast<float>(p.sampleRate);
    threshold_ = -24.0f + p.alpha * 24.0f;
    ratio_ = 1.0f + p.beta * 19.0f;
    float atMs = 5.0f + (1.0f - p.gamma) * 95.0f;
    float relMs = 50.0f + (1.0f - p.gamma) * 450.0f;
    attackCoef_ = std::exp(-1.0f / (sr_ * atMs * 0.001f));
    releaseCoef_ = std::exp(-1.0f / (sr_ * relMs * 0.001f));
    float reduction = threshold_ * (1.0f - 1.0f / ratio_);
    makeupGain_ = std::pow(10.0f, (-reduction * 0.5f) / 20.0f);
    inv_atk_ = 1.0f - attackCoef_;
    inv_rel_ = 1.0f - releaseCoef_;
    slope_ = 1.0f - 1.0f / ratio_;
}

void Compressor::setThreshold(float db) {
    threshold_ = db;
}

void Compressor::setRatio(float ratio) {
    ratio_ = ratio;
    slope_ = 1.0f - 1.0f / ratio_;
}

void Compressor::setAttack(float ms) {
    attackCoef_ = std::exp(-1.0f / (sr_ * ms * 0.001f));
    inv_atk_ = 1.0f - attackCoef_;
}

void Compressor::setRelease(float ms) {
    releaseCoef_ = std::exp(-1.0f / (sr_ * ms * 0.001f));
    inv_rel_ = 1.0f - releaseCoef_;
}

void Compressor::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;

    constexpr float k20DivLog2_10 = 6.0205999133f; // 20/log2(10) — para fastLog2 (base 2)
    constexpr float kLog2_10Div20 = 0.1660964047f; // log2(10)/20 — para fastExp2 (base 2)

    const float attackCoef = attackCoef_;
    const float releaseCoef = releaseCoef_;
    const float threshold = threshold_;
    const float ratioInv = 1.0f - 1.0f / ratio_;
    const float makeup = makeupGain_;
    float env = env_;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for (int i = 0; i < frames; ++i) {
        float peak = fmaxf(fabsf(left[i]), fabsf(right[i]));
        if (peak < 1e-6f) peak = 1e-6f;

        float coef = (peak > env)? attackCoef : releaseCoef;
        env = coef * env + (1.0f - coef) * peak;

        float envDb = k20DivLog2_10 * fastLog2(env);
        float gainDb = 0.0f;
        if (envDb > threshold) {
            gainDb = (threshold - envDb) * ratioInv;
        }

        float lin = makeup * fastExp2(gainDb * kLog2_10Div20);
        left[i] *= lin;
        right[i] *= lin;
    }
#pragma clang diagnostic pop

    env_ = env;
}

void Compressor::reset() {
    env_ = 0.0f;
}

} // namespace ivanna
