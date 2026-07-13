#pragma once
#include "dsp_types.h"
namespace ivanna {
class GainStage {
public:
    void setParams(const DSPParams& p);
    void processInput(float* left, float* right, int frames);
    void processOutput(float* left, float* right, int frames);
    void reset();

    // FASE 4C/P0 (cierre del Adaptive Feedback Loop): multiplicador
    // adicional sugerido por AdaptiveDecisionEngine::computeTargetGain(),
    // que por diseño está clampeado a [0.5, 1.0] — solo puede atenuar,
    // nunca subir por encima de la ganancia ya configurada por el usuario.
    // Se aplica DENTRO de processOutput() (multiplicando el target antes
    // del smoothing existente, currentOut_ -> outputGain_*runtimeMul_) —
    // se beneficia del mismo suavizado exponencial que ya tiene la clase
    // (~15ms), sin necesitar un smoothing propio en el JNI. No se toca
    // setParams()/outputGain_ (el valor base que controla el usuario) —
    // esto es una atenuación adicional temporal, no un reemplazo.
    void setRuntimeGain(float mul) noexcept {
        runtimeMul_ = mul < 0.1f ? 0.1f : (mul > 1.5f ? 1.5f : mul);
    }

private:
    float sr_ = 48000.0f;
    float smoothCoeff_ = 0.99f;
    float oneMinusSmooth_ = 0.01f;
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    float currentIn_ = 1.0f;
    float currentOut_ = 1.0f;
    float runtimeMul_ = 1.0f;
};
}
