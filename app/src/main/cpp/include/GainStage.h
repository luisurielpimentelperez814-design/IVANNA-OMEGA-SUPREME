#pragma once
#include "dsp_types.h"
namespace ivanna {
class GainStage {
public:
    void setParams(const DSPParams& p);
    void processInput(float* left, float* right, int frames);
    void processOutput(float* left, float* right, int frames);
    void reset();
private:
    float sr_ = 48000.0f;
    float smoothCoeff_ = 0.99f;
    float oneMinusSmooth_ = 0.01f;
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    float currentIn_ = 1.0f;
    float currentOut_ = 1.0f;
};
}
