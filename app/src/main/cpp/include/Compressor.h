#pragma once
#include "dsp_types.h"

namespace ivanna {

class Compressor {
public:
    Compressor();
    void setParams(const DSPParams& p);
    void setThreshold(float db);
    void setRatio(float ratio);
    void setAttack(float ms);
    void setRelease(float ms);
    void process(float* left, float* right, int frames);
    void reset();

    // FASE 4C/P0 (cierre del Adaptive Feedback Loop): compresión adicional
    // sugerida por AdaptiveDecisionEngine::computeCompressorAmount() (0..1,
    // basado en crest factor real). Se aplica DENTRO de process(): baja el
    // threshold efectivo hasta 12dB y sube el ratio efectivo hasta +8,
    // proporcional al amount — nunca toca makeupGain_ (que sigue calculado
    // solo desde los params base) para no introducir ganancia extra sin
    // control; el resultado de más compresión con la misma makeup es
    // SIEMPRE menor o igual nivel de pico, seguro por construcción. No se
    // toca setThreshold()/setRatio()/threshold_/ratio_ (los valores base
    // que controla el usuario) — esto se sustrae en tiempo de proceso, no
    // reemplaza el estado persistente.
    void setRuntimeAmount(float amount01) noexcept {
        runtimeAmount_ = amount01 < 0.f ? 0.f : (amount01 > 1.f ? 1.f : amount01);
    }

private:
    float sr_ = 96000.0f;
    float threshold_ = -12.0f;
    float ratio_ = 4.0f;
    float attackCoef_ = 0.99f;
    float releaseCoef_ = 0.999f;
    float makeupGain_ = 1.0f;
    float env_ = 0.0f;
    float runtimeAmount_ = 0.0f;

    [[maybe_unused]] float inv_atk_ = 1.0f;
    [[maybe_unused]] float inv_rel_ = 1.0f;
    [[maybe_unused]] float slope_ = 0.75f;
};

} // namespace ivanna
