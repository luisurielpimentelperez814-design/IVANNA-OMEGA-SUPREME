#include "../include/HarmonicExciter.h"
#include <cmath>
#include <algorithm>

namespace Ivanna {

HarmonicExciter::HarmonicExciter() {
    DSPParams defaultParams;
    setParams(defaultParams);
}

void HarmonicExciter::setParams(const DSPParams& params) {
    drive_ = params.exciterDrive;
    mix_ = params.exciterMix;
    harmonicsMix_ = params.exciterHarmonicsMix;

    float fc = 2400.0f / 48000.0f;
    float q = 0.7071f;
    hpfL_.setHighpass(fc, q);
    hpfR_.setHighpass(fc, q);
}

void HarmonicExciter::process(float* left, float* right, size_t numSamples) {
    if (mix_ <= 0.001f) return;

    for (size_t i = 0; i < numSamples; ++i) {
        float inL = left[i];
        float inR = right[i];

        float hpL = hpfL_.process(inL);
        float hpR = hpfR_.process(inR);

        float drivenL = hpL * (1.0f + drive_ * 3.0f);
        float drivenR = hpR * (1.0f + drive_ * 3.0f);

        auto exciteSample = [this](float x) {
            float x2 = x * x;
            float even = x2 * 0.5f;
            float odd = (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
            return (1.0f - harmonicsMix_) * odd + harmonicsMix_ * even;
        };

        float excL = exciteSample(drivenL);
        float excR = exciteSample(drivenR);

        left[i] = inL + excL * mix_ * runtimeReduction_;
        right[i] = inR + excR * mix_ * runtimeReduction_;
    }
}

void HarmonicExciter::reset() {
    hpfL_.reset();
    hpfR_.reset();
    osLeft_.reset();
    osRight_.reset();
}

} // namespace Ivanna
