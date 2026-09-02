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

    void setRuntimeAmount(float amount01) noexcept {
        // Solo fija el OBJETIVO — process() converge con one-pole (~20 ms).
        // El motor adaptativo llama esto por bloque y antes el valor entraba
        // directo a threshold (-12 dB) y ratio (+8): cada actualización era
        // un salto de ganancia audible en material sostenido (zipper adaptativo).
        runtimeTarget_ =
            amount01 < 0.f ? 0.f :
            (amount01 > 1.f ? 1.f : amount01);
    }

private:
    void recomputeMakeup() noexcept;

    float sr_ = 96000.0f;
    float threshold_ = -12.0f;
    float ratio_ = 4.0f;
    float attackCoef_ = 0.99f;
    float releaseCoef_ = 0.999f;

    float makeupGain_ = 1.0f;   // lineal (solo informativo/compat)
    float makeupDb_ = 0.0f;     // techo de compensacion en dB
    float env_ = 0.0f;
    float runtimeAmount_ = 0.0f;   // valor suavizado (uso real en process)
    float runtimeTarget_ = 0.0f;   // objetivo fijado por setRuntimeAmount
    uint32_t lastSr_ = 0;             // sample rate con el que se armo el sidechain
    float runtimeCoef_ = 0.0f;     // coef one-pole; 0 => recalcular en process

    Biquad scHpfL_;
    Biquad scHpfR_;
    // FIX: inv_atk_, inv_rel_, slope_ eran calculados en setAttack/setRelease
    // y recomputeMakeup() pero NUNCA leídos en process() — el compresor real
    // usa attackCoef_ y releaseCoef_ (coeficientes de one-pole). Los tres
    // campos [[maybe_unused]] añadían 12 bytes de estado falso por instancia,
    // confundían el análisis de cobertura, y podían enmascarar bugs futuros
    // (un linter podría marcar un campo como "usado" solo porque existe).
    // Eliminados: el comportamiento del compresor no cambia en absoluto.
};

}
