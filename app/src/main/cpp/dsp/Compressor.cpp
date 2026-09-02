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

    // slope_ eliminado del header (era dead state — nunca leído en process())
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

    // inv_atk_/inv_rel_ eliminados del header (dead state)

    runtimeCoef_ = 0.0f;  // forzar recálculo si cambió sr_ (ver process())

    // FIX (chasquidos al mover sliders): setParams() recalculaba el sidechain
    // HPF y ademas reseteaba su estado en CADA llamada. Arrastrar cualquier
    // fader (alpha/beta/gamma) vaciaba el historial del filtro -> el detector
    // veia un transitorio artificial -> salto de ganancia audible por cada
    // evento de UI. Los coeficientes solo dependen del sample rate, asi que se
    // recalculan (y el estado se limpia) unicamente cuando el sr cambia.
    if (p.sampleRate != lastSr_) {
        lastSr_ = p.sampleRate;

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
    // inv_atk_ eliminado del header
}

void Compressor::setRelease(float ms) {
    releaseCoef_ =
        std::exp(-1.0f / (sr_ * ms * 0.001f));
    // inv_rel_ eliminado del header
}


void Compressor::process(float* __restrict__ left,
                         float* __restrict__ right,
                         int frames) {

    if (frames <= 0) return;

    constexpr float k20DivLn10 = 8.6858896381f;
    constexpr float kLn10Div20 = 0.11512925465f;

    const float attackCoef = attackCoef_;
    const float releaseCoef = releaseCoef_;

    // Anti-zipper del runtimeAmount: el motor adaptativo fija el objetivo
    // por bloque (setRuntimeAmount); aquí se converge con un one-pole de
    // ~20 ms para que threshold/ratio no salten de golpe en sostenidos.
    // runtimeCoef_ se recalcula si sr_ cambió (queda 0 tras setParams).
    if (runtimeCoef_ <= 0.0f)
        runtimeCoef_ = std::exp(-1.0f / (sr_ * 0.020f));
    {
        const float rc = runtimeCoef_;
        runtimeAmount_ = rc * runtimeAmount_ + (1.0f - rc) * runtimeTarget_;
    }

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
