#if defined(__clang__)
#pragma clang optimize on
#else
#pragma GCC optimize("O3", "unroll-loops")
#endif
#include "../include/HarmonicExciter.h"
#include <cmath>
#include <cstring>

namespace ivanna {

HarmonicExciter::HarmonicExciter() {
    reset();
}

void HarmonicExciter::reset() {
    lastL_ = 0.0f;
    lastR_ = 0.0f;
    std::memset(osLeft_, 0, sizeof(osLeft_));
    std::memset(osRight_, 0, sizeof(osRight_));
}

void HarmonicExciter::setParams(const DSPParams& p) {
    drive_ = p.goldenEarDrive;
    wet_ = p.goldenEarMix;
    dry_ = 1.0f - p.goldenEarMix;
    harmonicGain_ = p.harmonicGain;

    double sampleRateOS = (double)p.sampleRate * (double)OS_FACTOR;
    double fc = 18000.0;
    if (fc > sampleRateOS * 0.45) fc = sampleRateOS * 0.45;
    double omegaOS = 2.0 * M_PI * fc / sampleRateOS;
    double swOS = std::sin(omegaOS);
    double cwOS = std::cos(omegaOS);
    double alphaOS = swOS / (2.0 * 0.707);
    double a0OS_inv = 1.0 / (1.0 + alphaOS);

    osLpfL_.b0 = (float)((1.0 + cwOS) * 0.5 * a0OS_inv);
    osLpfL_.b1 = (float)(-(1.0 + cwOS) * a0OS_inv);
    osLpfL_.b2 = osLpfL_.b0;
    osLpfL_.a1 = (float)(-2.0 * cwOS * a0OS_inv);
    osLpfL_.a2 = (float)((1.0 - alphaOS) * a0OS_inv);
    osLpfR_ = osLpfL_;

    excRelCoef_ = std::exp(-1.0f / ((float)p.sampleRate * OS_FACTOR * 0.020f));
}

static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    float absX = x < 0.0f ? -x : x;
    if (absX > 2.5f) {
        x = x > 0.0f ? (2.5f + 0.5f * std::tanh((x - 2.5f) * 0.5f)) : (-2.5f - 0.5f * std::tanh((-x - 2.5f) * 0.5f));
    }
    float x2 = x * x;
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

static constexpr float kExcCeiling = 0.98855f;

__attribute__((hot, flatten))
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0 || frames > MAX_OS_FRAMES) return;

    const float drive = drive_;
    const float wet   = wet_ * runtimeReductionMul_;
    const float dry   = dry_;

    int osIdx = 0;
    for (int i = 0; i < frames; ++i) {
        float l = left[i];
        float r = right[i];

        osLeft_[osIdx] = l;
        osRight_[osIdx] = r;
        osIdx++;

        float nextL = (i + 1 < frames) ? left[i + 1] : l;
        float nextR = (i + 1 < frames) ? right[i + 1] : r;

        osLeft_[osIdx] = 0.5f * (l + nextL);
        osRight_[osIdx] = 0.5f * (r + nextR);
        osIdx++;
    }
    int osFrames = osIdx;
    lastL_ = left[frames - 1];
    lastR_ = right[frames - 1];

    for (int i = 0; i < osFrames; ++i) {
        float l = osLeft_[i];
        float r = osRight_[i];

        float hL = hpfL_.process(l);
        float hR = hpfR_.process(r);

        float excL = softClip(hL, drive) - hL;
        float excR = softClip(hR, drive) - hR;

        excL = osLpfL_.process(excL);
        excR = osLpfR_.process(excR);

        float wl = wet * excL;
        float wr = wet * excR;

        left[i / 2] = l + wl;
        right[i / 2] = r + wr;
    }
}

} // namespace ivanna
