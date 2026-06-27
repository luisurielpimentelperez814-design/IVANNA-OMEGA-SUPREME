#pragma once
#include "dsp_types.h"

namespace ivanna {

// Input gain + output master with smooth ramping
class GainStage {
public:
    void setParams(const DSPParams& p);
    void processInput(float* left, float* right, int frames);
    void processOutput(float* left, float* right, int frames);
    void reset(); // FIX: reinicia rampeos para evitar clicks post-reset

private:
    float inputGain_  = 1.f;
    float outputGain_ = 1.f;
    float currentIn_  = 1.f;
    float currentOut_ = 1.f;
    uint32_t sr_ = 48000; // FIX #13: necesario para calcular smooth por SR
};

} // namespace ivanna
