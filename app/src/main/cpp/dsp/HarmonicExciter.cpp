#include "dsp/HarmonicExciter.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

HarmonicExciter::HarmonicExciter() {
    reset();
}

void HarmonicExciter::reset() {
    stateL_ = 0.0f;
    stateR_ = 0.0f;
    lpStateL_ = 0.0f;
    lpStateR_ = 0.0f;
    dcStateL_ = 0.0f;
    dcStateR_ = 0.0f;
    osBufferL_.fill(0.0f);
    osBufferR_.fill(0.0f);
}

void HarmonicExciter::setHarmonics(float h2, float h3, float h4) {
    h2_ = std::clamp(h2, 0.0f, 2.0f);
    h3_ = std::clamp(h3, 0.0f, 2.0f);
    h4_ = std::clamp(h4, 0.0f, 2.0f);
}

void HarmonicExciter::setMix(float drive, float wet, float dry) {
    drive_ = std::clamp(drive, 0.0f, 4.0f);
    wet_   = std::clamp(wet,   0.0f, 1.0f);
    dry_   = std::clamp(dry,   0.0f, 1.0f);
}

void HarmonicExciter::setSafetyGuard(float currentFatigueIndex, float acousticPressure) {
    if (currentFatigueIndex > 0.8f || acousticPressure > 0.9f) {
        runtimeReductionMul_ = 0.3f;
    } else if (currentFatigueIndex > 0.5f) {
        runtimeReductionMul_ = 0.6f;
    } else {
        runtimeReductionMul_ = 1.0f;
    }
}

void HarmonicExciter::process(float* left, float* right, int numSamples) {
    const float drive = drive_;
    const float wet   = wet_ * runtimeReductionMul_;
    const float dry   = dry_;
    (void)dry;

    for (int i = 0; i < numSamples; ++i) {
        float inL = left[i];
        float inR = right[i];

        float hpL = inL - lpStateL_;
        float hpR = inR - lpStateR_;
        lpStateL_ += 0.15f * (inL - lpStateL_);
        lpStateR_ += 0.15f * (inR - lpStateR_);

        float xL = hpL * (1.0f + drive);
        float xR = hpR * (1.0f + drive);

        float harmL = h2_ * (xL * xL) + h3_ * (xL * xL * xL) + h4_ * (xL * xL * xL * xL);
        float harmR = h2_ * (xR * xR) + h3_ * (xR * xR * xR) + h4_ * (xR * xR * xR * xR);

        harmL -= dcStateL_;
        harmR -= dcStateR_;
        dcStateL_ += 0.01f * harmL;
        dcStateR_ += 0.01f * harmR;

        left[i]  = inL + harmL * wet;
        right[i] = inR + harmR * wet;
    }
}

} // namespace ivanna
