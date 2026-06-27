#include "../include/HarmonicExciter.h"
#include <cmath>

namespace ivanna {

// Asymmetric soft-clip that generates 2nd + 3rd harmonics
static inline float softClip(float x, float drive) {
    x *= drive;
    // tanh approximation (Padé) — cheaper than std::tanh
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

void HarmonicExciter::setParams(const DSPParams& p) {
    drive_ = 1.f + p.drive * 15.f;  // 1..16
    wet_   = p.wet;
    dry_   = 1.f - p.wet;
    // HPF at 3 kHz: feed only high content into exciter
    double w0 = 2.0*M_PI*3000.0 / p.sampleRate;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * 0.707);
    double a0 = 1 + alpha;
    hpfL_.b0 = (1+cw)/(2*a0); hpfL_.b1 = -(1+cw)/a0; hpfL_.b2 = hpfL_.b0;
    hpfL_.a1 = -2*cw/a0;      hpfL_.a2 = (1-alpha)/a0;
    hpfR_ = hpfL_;
}

void HarmonicExciter::process(float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) {
        float hL = hpfL_.process(left[i]);
        float hR = hpfR_.process(right[i]);
        float excL = softClip(hL, drive_) - hL;  // only the generated harmonics
        float excR = softClip(hR, drive_) - hR;
        left[i]  = dry_ * left[i]  + wet_ * (left[i]  + excL);
        right[i] = dry_ * right[i] + wet_ * (right[i] + excR);
    }
}

void HarmonicExciter::reset() {
    hpfL_.reset(); hpfR_.reset();
}

} // namespace ivanna
