#include "../include/ParametricEQ.h"
#include <cmath>

namespace ivanna {

ParametricEQ::ParametricEQ() {
    reset();
}

void ParametricEQ::reset() {
    for (int i = 0; i < NUM_BANDS; ++i) {
        bandsL[i].reset();
        bandsR[i].reset();
    }
}

void ParametricEQ::setSampleRate(float sr) {
    sampleRate_ = sr;
}

void ParametricEQ::setBand(int band, float freq, float q, float gainDb) {
    if (band < 0 || band >= NUM_BANDS) return;

    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * M_PI * freq / sampleRate_;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha / A;

    // Normalizar
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;

    bandsL[band].b0 = b0; bandsL[band].b1 = b1; bandsL[band].b2 = b2;
    bandsL[band].a1 = a1; bandsL[band].a2 = a2;

    bandsR[band].b0 = b0; bandsR[band].b1 = b1; bandsR[band].b2 = b2;
    bandsR[band].a1 = a1; bandsR[band].a2 = a2;
}

void ParametricEQ::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for (int i = 0; i < frames; ++i) {
        float l = left[i];
        float r = right[i];

        // cascada de 8 biquads
        for (int b = 0; b < NUM_BANDS; ++b) {
            l = bandsL[b].processSample(l);
            r = bandsR[b].processSample(r);
        }

        left[i] = l;
        right[i] = r;
    }
#pragma clang diagnostic pop
}

} // namespace ivanna
