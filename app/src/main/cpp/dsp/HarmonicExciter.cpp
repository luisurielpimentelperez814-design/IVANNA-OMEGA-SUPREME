#if defined(__clang__)
#pragma clang optimize on
#else
#pragma GCC optimize("O3", "unroll-loops")
#endif
#include "../include/HarmonicExciter.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ivanna {

void HarmonicExciter::reset() {
    lastL_ = 0.0f;
    lastR_ = 0.0f;
    std::memset(osLeft_, 0, sizeof(osLeft_));
    std::memset(osRight_, 0, sizeof(osRight_));
    hpfL_.reset();
    hpfR_.reset();
    osLpfL_.reset();
    osLpfR_.reset();
    preLpfL_.reset();
    preLpfR_.reset();
    chebL_.reset();
    chebR_.reset();
    excScaleL_ = 1.0f;
    excScaleR_ = 1.0f;
    fundCrossL_ = fundPowL_ = fundGainL_ = 0.0f;
    fundCrossR_ = fundPowR_ = fundGainR_ = 0.0f;
    wetNow_ = wet_;
    driveNow_ = drive_;
    lastSampleRate_ = 0;
}

void HarmonicExciter::setParams(const DSPParams& p) {
    drive_ = 1.0f + p.drive * 3.0f;
    wet_ = p.wet;
    dry_ = 1.0f - p.wet;

    const int srInt = p.sampleRate > 0 ? p.sampleRate : 48000;
    if (srInt == lastSampleRate_) {
        return;
    }
    lastSampleRate_ = srInt;

    {
        const double srOS = (double)srInt * (double)OS_FACTOR;
        wetSmooth_ = (float)std::exp(-1.0 / (srOS * 0.015));
        driveSmooth_ = wetSmooth_;
        chebL_.prepare((float)srOS);
        chebR_.prepare((float)srOS);
    }

    double sampleRateOS = (double)srInt * (double)OS_FACTOR;

    double fc = 12000.0;
    if (fc > sampleRateOS * 0.45) fc = sampleRateOS * 0.45;
    double omegaOS = 2.0 * M_PI * fc / sampleRateOS;
    double swOS = std::sin(omegaOS);
    double cwOS = std::cos(omegaOS);
    double alphaOS = swOS / (2.0 * 0.707);
    double a0OS_inv = 1.0 / (1.0 + alphaOS);

    osLpfL_.b0 = (float)((1.0 - cwOS) * 0.5 * a0OS_inv);
    osLpfL_.b1 = (float)((1.0 - cwOS) * a0OS_inv);
    osLpfL_.b2 = osLpfL_.b0;
    osLpfL_.a1 = (float)(-2.0 * cwOS * a0OS_inv);
    osLpfL_.a2 = (float)((1.0 - alphaOS) * a0OS_inv);
    osLpfR_.b0 = osLpfL_.b0;
    osLpfR_.b1 = osLpfL_.b1;
    osLpfR_.b2 = osLpfL_.b2;
    osLpfR_.a1 = osLpfL_.a1;
    osLpfR_.a2 = osLpfL_.a2;

    {
        double sr = (double)srInt;
        double fc_pre = 8000.0;
        if (fc_pre > sr * 0.45) fc_pre = sr * 0.45;
        double K = std::tan(M_PI * fc_pre / sr);
        double KK = K * K;
        double Q = 0.707106781;
        double norm_pre = 1.0 + K / Q + KK;
        preLpfL_.b0 = (float)(KK / norm_pre);
        preLpfL_.b1 = (float)(2.0 * KK / norm_pre);
        preLpfL_.b2 = preLpfL_.b0;
        preLpfL_.a1 = (float)(2.0 * (KK - 1.0) / norm_pre);
        preLpfL_.a2 = (float)((1.0 - K / Q + KK) / norm_pre);
        preLpfR_.b0 = preLpfL_.b0;
        preLpfR_.b1 = preLpfL_.b1;
        preLpfR_.b2 = preLpfL_.b2;
        preLpfR_.a1 = preLpfL_.a1;
        preLpfR_.a2 = preLpfL_.a2;
    }

    double hpfFc = 3000.0;
    if (hpfFc > sampleRateOS * 0.45) hpfFc = sampleRateOS * 0.45;
    hpfL_.setHighpass(hpfFc, 0.707, sampleRateOS);
    hpfR_.setHighpass(hpfFc, 0.707, sampleRateOS);

    excRelCoef_ = std::exp(-1.0f / ((float)srInt * OS_FACTOR * 0.020f));
    // Promedio de la regresión de fundamental: 3 ms (>= 9 periodos del contenido
    // que pasa el HPF de 3 kHz → rizado de 2f atenuado > 35 dB).
    fundCoef_ = std::exp(-1.0f / ((float)srInt * OS_FACTOR * 0.003f));
}

static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    float absX = x < 0.0f ? -x : x;
    if (absX > 3.0f) {
        x = x > 0.0f ? (3.0f + 0.5f * std::tanh((x - 3.0f) * 0.5f))
                     : (-3.0f - 0.5f * std::tanh((-x - 3.0f) * 0.5f));
    }
    float x2 = x * x;
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

static constexpr float kExcCeiling = 0.98855f;

__attribute__((hot, flatten))
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0 || frames > MAX_OS_FRAMES) return;

    const float wetTarget = wet_ * runtimeReductionMul_;
    float wetNow = wetNow_;
    const float wetSm = wetSmooth_ > 0.f ? wetSmooth_ : 0.9995f;

    if (wetTarget <= 0.00001f && wetNow <= 0.00001f) {
        wetNow_ = 0.0f;
        return;
    }

    driveNow_ = driveSmooth_ * driveNow_ + (1.0f - driveSmooth_) * drive_;
    const float drive = driveNow_;
    const float dNorm = std::clamp((drive - 1.0f) / 3.0f, 0.0f, 1.0f);

    float scaleL = excScaleL_;
    float scaleR = excScaleR_;
    const float rel = excRelCoef_;

    float preFiltL[MAX_OS_FRAMES];
    float preFiltR[MAX_OS_FRAMES];
    for (int i = 0; i < frames; ++i) {
        preFiltL[i] = preLpfL_.process(left[i]);
        preFiltR[i] = preLpfR_.process(right[i]);
    }

    int osIdx = 0;
    for (int i = 0; i < frames; ++i) {
        osLeft_[osIdx]  = preFiltL[i];
        osRight_[osIdx] = preFiltR[i];
        osIdx++;

        float nextLF = (i + 1 < frames) ? preFiltL[i + 1] : preFiltL[i];
        float nextRF = (i + 1 < frames) ? preFiltR[i + 1] : preFiltR[i];
        osLeft_[osIdx]  = 0.5f * (preFiltL[i] + nextLF);
        osRight_[osIdx] = 0.5f * (preFiltR[i] + nextRF);
        osIdx++;
    }
    int osFrames = osIdx;
    lastL_ = left[frames - 1];
    lastR_ = right[frames - 1];

    float dryCacheL = 0.0f, dryCacheR = 0.0f;
    for (int i = 0; i < osFrames; ++i) {
        float l = osLeft_[i];
        float r = osRight_[i];

        float hL = hpfL_.process(l);
        float hR = hpfR_.process(r);

        float excL = 0.0f, excR = 0.0f;
        if (shaperMode_ == 1) {
            const float chebOutL = chebL_.tick(hL * drive, dNorm, warmth_, flatness1m_);
            const float chebOutR = chebR_.tick(hR * drive, dNorm, warmth_, flatness1m_);
            const float padeL = softClip(hL, drive) - hL;
            const float padeR = softClip(hR, drive) - hR;
            excL = 0.65f * chebOutL + 0.35f * padeL;
            excR = 0.65f * chebOutR + 0.35f * padeR;
        } else {
            excL = softClip(hL, drive) - hL;
            excR = softClip(hR, drive) - hR;
        }

        // SOLO ARMÓNICOS: el shaper (softClip−x o Chebyshev) trae una componente lineal
        // en la fundamental (medido: +7.5 dB a 3.5 kHz con drive .6 / wet .8, nivel -20 dBFS)
        // que se sumaba al seco como realce de 3-8 kHz dependiente del nivel. Se resta la
        // parte en fase con la entrada del shaper; queda H2/H3/... sin ganancia lineal.
        {
            fundCrossL_ = fundCoef_ * fundCrossL_ + (1.0f - fundCoef_) * (excL * hL);
            fundPowL_   = fundCoef_ * fundPowL_   + (1.0f - fundCoef_) * (hL * hL);
            fundCrossR_ = fundCoef_ * fundCrossR_ + (1.0f - fundCoef_) * (excR * hR);
            fundPowR_   = fundCoef_ * fundPowR_   + (1.0f - fundCoef_) * (hR * hR);
            if (fundPowL_ > 1.0e-8f) fundGainL_ = std::clamp(fundCrossL_ / fundPowL_, -8.0f, 8.0f);
            if (fundPowR_ > 1.0e-8f) fundGainR_ = std::clamp(fundCrossR_ / fundPowR_, -8.0f, 8.0f);
            excL -= fundGainL_ * hL;
            excR -= fundGainR_ * hR;
        }

        excL = osLpfL_.process(excL);
        excR = osLpfR_.process(excR);

        // Referencia SECA original: a índice impar left[i>>1] ya contiene la salida del
        // índice par (procesada). Se usa el seco interpolado (par: muestra; impar: punto
        // medio con la siguiente, aún sin tocar).
        float dryL, dryR;
        if ((i & 1) == 0) {
            dryL = left[i >> 1];
            dryR = right[i >> 1];
            dryCacheL = dryL;
            dryCacheR = dryR;
        } else {
            const int k1 = (i >> 1) + 1;
            const float nxL = (k1 < frames) ? left[k1]  : dryCacheL;
            const float nxR = (k1 < frames) ? right[k1] : dryCacheR;
            dryL = 0.5f * (dryCacheL + nxL);
            dryR = 0.5f * (dryCacheR + nxR);
        }
        const float peakRefL = std::max(std::fabs(l), std::fabs(dryL));
        const float peakRefR = std::max(std::fabs(r), std::fabs(dryR));
        const float headL = std::max(0.0f, 1.0f - peakRefL);
        const float headR = std::max(0.0f, 1.0f - peakRefR);
        const float reqL = kExcCeiling * wetNow * std::fabs(excL);
        const float reqR = kExcCeiling * wetNow * std::fabs(excR);
        const float needL = (reqL > headL) ? (headL / (reqL > 1e-9f ? reqL : 1e-9f)) : 1.0f;
        const float needR = (reqR > headR) ? (headR / (reqR > 1e-9f ? reqR : 1e-9f)) : 1.0f;
        if (needL < scaleL) scaleL = needL; else scaleL = rel * scaleL + (1.0f - rel);
        if (needR < scaleR) scaleR = needR; else scaleR = rel * scaleR + (1.0f - rel);
        scaleL = std::clamp(scaleL, 0.0f, 1.0f);
        scaleR = std::clamp(scaleR, 0.0f, 1.0f);

        float outL = dryL + kExcCeiling * wetNow * excL * scaleL;
        float outR = dryR + kExcCeiling * wetNow * excR * scaleR;

        wetNow = wetSm * wetNow + (1.0f - wetSm) * wetTarget;
        wetNow_ = wetNow;

        const float ceilL = std::max(1.0f, std::fabs(dryL));
        const float ceilR = std::max(1.0f, std::fabs(dryR));
        if (outL > ceilL) outL = ceilL; else if (outL < -ceilL) outL = -ceilL;
        if (outR > ceilR) outR = ceilR; else if (outR < -ceilR) outR = -ceilR;

        if (!std::isfinite(outL)) outL = 0.f;
        if (!std::isfinite(outR)) outR = 0.f;

        if ((i & 1) == 0) {
            left[i >> 1]  = outL;
            right[i >> 1] = outR;
        }
    }

    excScaleL_ = scaleL;
    excScaleR_ = scaleR;
}

} // namespace ivanna
