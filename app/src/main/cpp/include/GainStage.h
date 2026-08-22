#pragma once
#include "dsp_types.h"
#include <cmath>
namespace ivanna {
class GainStage {
public:
    void setParams(const DSPParams& p);
    void processInput(float* left, float* right, int frames);
    void processOutput(float* left, float* right, int frames);
    void reset();

    void setRuntimeGain(float mul) noexcept {
        runtimeMul_ = mul < 0.1f ? 0.1f : (mul > 1.5f ? 1.5f : mul);
    }

private:
    float sr_ = 96000.0f;
    float smoothCoeff_ = 0.99f;
    float oneMinusSmooth_ = 0.01f;
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    float currentIn_ = 1.0f;
    float currentOut_ = 1.0f;
    float runtimeMul_ = 1.0f;

    // FIX (ruido digital): aqui vivia un limitador brick-wall de 1 muestra de
    // ataque, en cascada delante del SafetyLimiter. Dos limiters en serie con
    // ataque instantaneo modulan la senal muestra a muestra y generan
    // intermodulacion de banda ancha. El SafetyLimiter final ya garantiza el
    // techo de -0.1 dBFS con escalado por bloque (sin AM), asi que esta etapa
    // ahora es solo ganancia suavizada, lineal y transparente.
};
}
