#if defined(__clang__)
#pragma clang optimize on
#else
#pragma GCC optimize("O3", "unroll-loops")
#endif

#include "../include/Compressor.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

Compressor::Compressor() {
    reset();
}

void Compressor::recomputeMakeup() noexcept {
    // FIX (ruido digital): makeupGain_ se aplicaba como ganancia estatica
    // (hasta ~+11 dB) incluso con la senal por debajo del threshold, es decir
    // sin ninguna reduccion que compensar. Eso empujaba material ya limpio
    // contra el SafetyLimiter, que entraba a trabajar sin razon.
    // Ahora se guarda en dB y en process() se limita a la reduccion REAL
    // instantanea: nunca se devuelve mas de lo que se quito.
    const float reduction =
        threshold_ * (1.0f - 1.0f / ratio_);

    makeupDb_ = -reduction * 0.5f;      // objetivo maximo de compensacion
    if (makeupDb_ < 0.0f) makeupDb_ = 0.0f;

    makeupGain_ = std::pow(10.0f, makeupDb_ / 20.0f);

    slope_ = 1.0f - 1.0f / ratio_;
}

void Compressor::setParams(const DSPParams& p) {
    sr_ = static_cast<float>(p.sampleRate);

    threshold_ = -24.0f + p.alpha * 24.0f;
    ratio_ = 1.0f + p.beta * 19.0f;

    float atMs = 5.0f + (1.0f - p.gamma) * 95.0f;
    float relMs = 50.0f + (1.0f - p.gamma) * 450.0f;

    attackCoef_ =
        std::exp(-1.0f / (sr_ * atMs * 0.001f));

    releaseCoef_ =
        std::exp(-1.0f / (sr_ * relMs * 0.001f));

    recomputeMakeup();

    inv_atk_ = 1.0f - attackCoef_;
    inv_rel_ = 1.0f - releaseCoef_;

    scHpfL_.setHighpass(
        120.0,
        0.70710678,
        static_cast<double>(p.sampleRate));

    scHpfR_.setHighpass(
        120.0,
        0.70710678,
        static_cast<double>(p.sampleRate));

    scHpfL_.reset();
    scHpfR_.reset();
}

void Compressor::setThreshold(float db) {
    threshold_ = db;
    recomputeMakeup();
}

void Compressor::setRatio(float ratio) {
    ratio_ = ratio < 1.0f ? 1.0f : ratio;
    recomputeMakeup();
}

void Compressor::setAttack(float ms) {
    attackCoef_ =
        std::exp(-1.0f / (sr_ * ms * 0.001f));
    inv_atk_ = 1.0f - attackCoef_;
}

void Compressor::setRelease(float ms) {
    releaseCoef_ =
        std::exp(-1.0f / (sr_ * ms * 0.001f));
    inv_rel_ = 1.0f - releaseCoef_;
}


void Compressor::process(float* __restrict__ left,
                         float* __restrict__ right,
                         int frames) {

    if (frames <= 0) return;

    constexpr float k20DivLn10 = 8.6858896381f;
    constexpr float kLn10Div20 = 0.11512925465f;

    const float attackCoef = attackCoef_;
    const float releaseCoef = releaseCoef_;

    const float threshold =
        threshold_ - runtimeAmount_ * 12.0f;

    const float effRatio =
        ratio_ + runtimeAmount_ * 8.0f;

    const float ratioInv =
        1.0f - 1.0f / effRatio;

    const float grAdditional =
        runtimeAmount_ * 12.0f *
        (1.0f - 1.0f / effRatio);

    // Techo de compensacion en dB (estatico + aporte del runtime).
    const float makeupCeilDb =
        makeupDb_ + grAdditional * 0.10f;

    float env = env_;

    for (int i = 0; i < frames; ++i) {

        const float dL =
            scHpfL_.process(left[i]);

        const float dR =
            scHpfR_.process(right[i]);

        float peak =
            std::fmax(std::fabs(dL),
                      std::fabs(dR));

        if (peak < 1e-6f)
            peak = 1e-6f;

        float coef =
            (peak > env) ?
            attackCoef :
            releaseCoef;

        env =
            coef * env +
            (1.0f - coef) * peak;


        float envDb =
            k20DivLn10 * std::log(env);

        float gainDb = 0.0f;

        constexpr float kKneeHalf = 3.0f;

        float overDb =
            envDb - threshold;


        if (overDb <= -kKneeHalf) {

            gainDb = 0.0f;

        } else if (overDb < kKneeHalf) {

            float t =
                (overDb + kKneeHalf) /
                (2.0f * kKneeHalf);

            gainDb =
                -(ratioInv * kKneeHalf)
                * t * t;

        } else {

            gainDb =
                overDb * (-ratioInv);

        }


        // Makeup acoplado a la reduccion real: gainDb es <= 0 (o 0 si no hay
        // compresion), asi que -gainDb es la reduccion aplicada en dB. Con la
        // senal bajo threshold no hay reduccion -> makeup 0 dB -> unidad
        // exacta, sin ganancia gratuita hacia el limiter.
        const float grDb = -gainDb;
        const float makeupDb =
            grDb < makeupCeilDb ? grDb : makeupCeilDb;

        float lin =
            std::exp((gainDb + makeupDb) * kLn10Div20);

        left[i] *= lin;
        right[i] *= lin;
    }

    env_ = env;
}


void Compressor::reset() {

    env_ = 0.0f;

    scHpfL_.reset();
    scHpfR_.reset();
}

}
