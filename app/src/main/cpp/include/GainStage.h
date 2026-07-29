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

    // Limitador true-peak brick-wall — 1 muestra de ataque, release ~50ms
    float limGain_    = 1.0f;   // ganancia actual del limitador
    float limRelCoef_ = 0.999f; // release coefficient (recalculado en setParams)
    static constexpr float LIM_THRESHOLD = 0.9886f; // −0.1 dBFS true-peak
};
}
