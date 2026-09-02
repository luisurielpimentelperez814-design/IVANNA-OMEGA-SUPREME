#include "HarmonicExciter.h"
#include <algorithm>
#include <cmath>

HarmonicExciter::HarmonicExciter() {
    reset();
}

void HarmonicExciter::setParams(const DSPParams& params) {
    drive_ = params.exciterDrive;
    harmonicsMix_ = params.exciterMix;
    setHighpassCutoff(params.exciterCutoffHz, params.sampleRate);
}

void HarmonicExciter::setHighpassCutoff(float cutoffHz, float sampleRate) {
    if (sampleRate <= 0.0f) return;
    hpfL_.setHighpass(cutoffHz, sampleRate);
    hpfR_.setHighpass(cutoffHz, sampleRate);
}

void HarmonicExciter::setRuntimeReduction(float factor) {
    runtimeReduction_ = std::clamp(factor, 0.0f, 1.0f);
}

void HarmonicExciter::reset() {
    hpfL_.reset();
    hpfR_.reset();
    osLeft_.reset();
    osRight_.reset();
}

void HarmonicExciter::process(float* inL, float* inR, float* outL, float* outR, size_t numFrames) {
    float effDrive = drive_ * (1.0f - 0.5f * runtimeReduction_);
    float effMix = harmonicsMix_ * (1.0f - 0.5f * runtimeReduction_);

    for (size_t i = 0; i < numFrames; ++i) {
        float hpL = hpfL_.process(inL[i]);
        float hpR = hpfR_.process(inR[i]);

        // Sobremuestreo x2 para mitigar aliasing armónico
        float osInL[2] = { hpL, 0.0f };
        float osInR[2] = { hpR, 0.0f };
        float osOutL[2];
        float osOutR[2];

        osLeft_.upsample2x(osInL, osOutL);
        osRight_.upsample2x(osInR, osOutR);

        for (int k = 0; k < OS_FACTOR; ++k) {
            float xL = osOutL[k] * effDrive;
            float xR = osOutR[k] * effDrive;

            // Saturación asimétrica para armónicos pares e impares
            osOutL[k] = std::tanh(xL + 0.1f * xL * xL);
            osOutR[k] = std::tanh(xR + 0.1f * xR * xR);
        }

        float excL = osLeft_.downsample2x(osOutL);
        float excR = osRight_.downsample2x(osOutR);

        outL[i] = inL[i] + excL * effMix;
        outR[i] = inR[i] + excR * effMix;
    }
}
