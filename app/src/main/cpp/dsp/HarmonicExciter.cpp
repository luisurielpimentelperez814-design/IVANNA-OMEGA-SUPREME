#include "HarmonicExciter.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

HarmonicExciter::HarmonicExciter(float sampleRate)
    : sampleRate_(sampleRate)
{
    reset();
}

void HarmonicExciter::setDrive(float drive) {
    drive_ = std::clamp(drive, 0.0f, 10.0f);
}

void HarmonicExciter::setHarmonicsMix(float evenHarmonics, float oddHarmonics) {
    evenHarmonics_ = std::clamp(evenHarmonics, 0.0f, 1.0f);
    oddHarmonics_  = std::clamp(oddHarmonics, 0.0f, 1.0f);
}

void HarmonicExciter::setMix(float wet, float dry) {
    wet_ = std::clamp(wet, 0.0f, 1.0f);
    dry_ = std::clamp(dry, 0.0f, 1.0f);
}

void HarmonicExciter::setHighpassCutoff(float fc) {
    hpCutoff_ = std::clamp(fc, 500.0f, 10000.0f);
    updateHighpassCoeffs();
}

void HarmonicExciter::setRuntimeReduction(float factor) {
    runtimeReductionMul_ = std::clamp(factor, 0.1f, 1.0f);
}

void HarmonicExciter::reset() {
    hpX1_L = hpX2_L = hpY1_L = hpY2_L = 0.0f;
    hpX1_R = hpX2_R = hpY1_R = hpY2_R = 0.0f;
    lpStateL_ = lpStateR_ = 0.0f;
    updateHighpassCoeffs();
}

void HarmonicExciter::updateHighpassCoeffs() {
    float w0 = 2.0f * 3.14159265358979323846f * hpCutoff_ / sampleRate_;
    float cosw0 = std::cos(w0);
    float alpha = std::sin(w0) / (2.0f * 0.70710678118654752440f);

    float b0 = (1.0f + cosw0) / 2.0f;
    float b1 = -(1.0f + cosw0);
    float b2 = (1.0f + cosw0) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;

    hpB0_ = b0 / a0;
    hpB1_ = b1 / a0;
    hpB2_ = b2 / a0;
    hpA1_ = a1 / a0;
    hpA2_ = a2 / a0;
}

void HarmonicExciter::process(float* left, float* right, int numSamples) {
    if (!left || !right || numSamples <= 0) return;

    const float drive = drive_;
    const float wet   = wet_ * runtimeReductionMul_;
    const float dry   = dry_;
    (void)dry;

    int osIdx = 0;
    for (int i = 0; i < numSamples; ++i) {
        float inL = left[i];
        float inR = right[i];

        float hpL = hpB0_ * inL + hpB1_ * hpX1_L + hpB2_ * hpX2_L - hpA1_ * hpY1_L - hpA2_ * hpY2_L;
        hpX2_L = hpX1_L; hpX1_L = inL;
        hpY2_L = hpY1_L; hpY1_L = hpL;

        float hpR = hpB0_ * inR + hpB1_ * hpX1_R + hpB2_ * hpX2_R - hpA1_ * hpY1_R - hpA2_ * hpY2_R;
        hpX2_R = hpX1_R; hpX1_R = inR;
        hpY2_R = hpY1_R; hpY1_R = hpR;

        float exL = (hpL * drive) - (evenHarmonics_ * hpL * hpL) + (oddHarmonics_ * hpL * hpL * hpL);
        float exR = (hpR * drive) - (evenHarmonics_ * hpR * hpR) + (oddHarmonics_ * hpR * hpR * hpR);

        lpStateL_ += 0.5f * (exL - lpStateL_);
        lpStateR_ += 0.5f * (exR - lpStateR_);

        left[i]  += wet * lpStateL_;
        right[i] += wet * lpStateR_;
    }
}

} // namespace ivanna
