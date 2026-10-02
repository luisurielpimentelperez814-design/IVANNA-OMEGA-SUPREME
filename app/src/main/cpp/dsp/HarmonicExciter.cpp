/**
 * HarmonicExciter.cpp — Implementation
 * Copyright (C) 2026 IVANNA-OMEGA Project
 */

#include "HarmonicExciter.h"
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ivanna {

HarmonicExciter::HarmonicExciter() {
    computeFilterCoeffs();
    chebL_.prepare(sampleRate_ * 2.0f);
    chebR_.prepare(sampleRate_ * 2.0f);
}

void HarmonicExciter::setParams(const DSPParams& params) {
    // Map drive [0,1] → [1.0, 16.0] internal multiplier
    const float d = std::isfinite(params.drive) ? std::clamp(params.drive, 0.0f, 1.0f) : 0.2f;
    driveNorm_ = d;
    drive_ = 1.0f + d * 15.0f;
    wet_   = std::isfinite(params.wet) ? std::clamp(params.wet, 0.0f, 1.0f) : 0.15f;
    if (params.sampleRate > 0 && static_cast<float>(params.sampleRate) != sampleRate_) {
        sampleRate_ = static_cast<float>(params.sampleRate);
        computeFilterCoeffs();
        chebL_.prepare(sampleRate_ * 2.0f);
        chebR_.prepare(sampleRate_ * 2.0f);
    }
}

void HarmonicExciter::setWarmth(float warmth) noexcept {
    if (std::isfinite(warmth)) {
        warmth_ = std::clamp(warmth, 0.0f, 1.0f);
    }
}

void HarmonicExciter::setFlatness1m(float f8) noexcept {
    if (std::isfinite(f8)) {
        flatness1m_ = std::clamp(f8, 0.0f, 1.0f);
    }
}

void HarmonicExciter::reset() {
    hpfStateL_.reset();
    hpfStateR_.reset();
    lpfUpL_.reset();
    lpfUpR_.reset();
    lpfDownL_.reset();
    lpfDownR_.reset();
    chebL_.reset();
    chebR_.reset();
}

void HarmonicExciter::computeFilterCoeffs() {
    // 2nd-order Butterworth HPF at 2800 Hz
    {
        float fc = 2800.0f;
        float w0 = 2.0f * static_cast<float>(M_PI) * fc / sampleRate_;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / static_cast<float>(M_SQRT2); // Q = 1/sqrt(2)

        float b0 =  (1.0f + cosw0) / 2.0f;
        float b1 = -(1.0f + cosw0);
        float b2 =  (1.0f + cosw0) / 2.0f;
        float a0 =   1.0f + alpha;
        float a1 =  -2.0f * cosw0;
        float a2 =   1.0f - alpha;

        hpfCoeffs_.b0 = b0 / a0;
        hpfCoeffs_.b1 = b1 / a0;
        hpfCoeffs_.b2 = b2 / a0;
        hpfCoeffs_.a1 = a1 / a0;
        hpfCoeffs_.a2 = a2 / a0;
    }

    // 2nd-order Butterworth LPF at 0.42 * fs (for 2x oversampled stream → 0.21 * 2fs)
    {
        float fc = 0.21f * (2.0f * sampleRate_);
        float w0 = 2.0f * static_cast<float>(M_PI) * fc / (2.0f * sampleRate_);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / static_cast<float>(M_SQRT2);

        float b0 =  (1.0f - cosw0) / 2.0f;
        float b1 =   1.0f - cosw0;
        float b2 =  (1.0f - cosw0) / 2.0f;
        float a0 =   1.0f + alpha;
        float a1 =  -2.0f * cosw0;
        float a2 =   1.0f - alpha;

        lpfCoeffs_.b0 = b0 / a0;
        lpfCoeffs_.b1 = b1 / a0;
        lpfCoeffs_.b2 = b2 / a0;
        lpfCoeffs_.a1 = a1 / a0;
        lpfCoeffs_.a2 = a2 / a0;
    }
}

// Padé [3/2] rational approximation of tanh(x), bounded to [-1, +1] (legacy fallback mode 0)
float HarmonicExciter::softClip(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Direct Form II Transposed biquad tick
float HarmonicExciter::tickBiquad(float x, const BiquadCoeffs& c, BiquadState& s) noexcept {
    if (!std::isfinite(x)) x = 0.0f;
    float y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    if (!std::isfinite(y) || !std::isfinite(s.z1) || !std::isfinite(s.z2)) {
        s.reset();
        return 0.0f;
    }
    if (std::fabs(s.z1) < 1e-20f) s.z1 = 0.0f;
    if (std::fabs(s.z2) < 1e-20f) s.z2 = 0.0f;
    return y;
}

void HarmonicExciter::upsample2x(const float* in, float* out, size_t numFrames,
                                  BiquadState& lpfState) noexcept {
    for (size_t n = 0; n < numFrames; ++n) {
        out[2 * n]     = tickBiquad(in[n] * 2.0f, lpfCoeffs_, lpfState);
        out[2 * n + 1] = tickBiquad(0.0f,         lpfCoeffs_, lpfState);
    }
}

void HarmonicExciter::downsample2x(const float* in, float* out, size_t numFrames,
                                    BiquadState& lpfState) noexcept {
    for (size_t n = 0; n < numFrames; ++n) {
        float y0 = tickBiquad(in[2 * n],     lpfCoeffs_, lpfState);
        (void)     tickBiquad(in[2 * n + 1], lpfCoeffs_, lpfState);
        out[n] = y0;
    }
}

void HarmonicExciter::process(float* left, float* right, size_t numFrames) {
    if (numFrames == 0) return;

    static constexpr size_t kMaxStackFrames = 2048;
    float stackHpfL[kMaxStackFrames], stackHpfR[kMaxStackFrames];
    float stackUpL [kMaxStackFrames * 2], stackUpR [kMaxStackFrames * 2];
    float stackExcL[kMaxStackFrames * 2], stackExcR[kMaxStackFrames * 2];
    float stackDownL[kMaxStackFrames], stackDownR[kMaxStackFrames];

    std::vector<float> heapBuf;
    float *hpfL, *hpfR, *upL, *upR, *excL, *excR, *downL, *downR;

    if (numFrames <= kMaxStackFrames) {
        hpfL  = stackHpfL;  hpfR  = stackHpfR;
        upL   = stackUpL;   upR   = stackUpR;
        excL  = stackExcL;  excR  = stackExcR;
        downL = stackDownL; downR = stackDownR;
    } else {
        heapBuf.resize(numFrames * 12);
        hpfL  = heapBuf.data();
        hpfR  = hpfL  + numFrames;
        upL   = hpfR  + numFrames;
        upR   = upL   + numFrames * 2;
        excL  = upR   + numFrames * 2;
        excR  = excL  + numFrames * 2;
        downL = excR  + numFrames * 2;
        downR = downL + numFrames;
    }

    // Step 1: High-pass filter to isolate excitation band (> 2.8 kHz)
    for (size_t n = 0; n < numFrames; ++n) {
        hpfL[n] = tickBiquad(left[n],  hpfCoeffs_, hpfStateL_);
        hpfR[n] = tickBiquad(right[n], hpfCoeffs_, hpfStateR_);
    }

    // Step 2: 2x upsample the high-passed signal
    upsample2x(hpfL, excL, numFrames, lpfUpL_);
    upsample2x(hpfR, excR, numFrames, lpfUpR_);

    // Store dry signal in upL/upR (interleaved per sample for convenience)
    for (size_t n = 0; n < numFrames; ++n) {
        upL[n * 2]     = left[n];
        upL[n * 2 + 1] = right[n];
    }

    // Step 3: Harmonic generation (M9 Chebyshev T2+T3 by default, or mode 0 softClip fallback)
    const size_t upLen = numFrames * 2;
    if (shaperMode_ == 1) {
        for (size_t i = 0; i < upLen; ++i) {
            excL[i] = chebL_.tick(excL[i], driveNorm_, warmth_, flatness1m_);
            excR[i] = chebR_.tick(excR[i], driveNorm_, warmth_, flatness1m_);
        }
    } else {
        for (size_t i = 0; i < upLen; ++i) {
            excL[i] = softClip(excL[i] * drive_);
            excR[i] = softClip(excR[i] * drive_);
        }
    }

    // Step 4: 2x downsample back to original rate
    downsample2x(excL, downL, numFrames, lpfDownL_);
    downsample2x(excR, downR, numFrames, lpfDownR_);

    // Step 5: Mix excited harmonics back into dry signal with headroom protection
    for (size_t n = 0; n < numFrames; ++n) {
        const float dryL = upL[n * 2];
        const float dryR = upL[n * 2 + 1];
        float wetL = wet_ * excScale_ * downL[n];
        float wetR = wet_ * excScale_ * downR[n];

        const float absDryL = std::fabs(dryL);
        const float absWetL = std::fabs(wetL);
        if (absDryL + absWetL > 1.0f && absWetL > 1e-12f) {
            const float room = std::max(0.0f, 1.0f - absDryL);
            wetL *= room / absWetL;
        }
        const float absDryR = std::fabs(dryR);
        const float absWetR = std::fabs(wetR);
        if (absDryR + absWetR > 1.0f && absWetR > 1e-12f) {
            const float room = std::max(0.0f, 1.0f - absDryR);
            wetR *= room / absWetR;
        }

        left[n]  = std::clamp(dryL + wetL, -1.0f, 1.0f);
        right[n] = std::clamp(dryR + wetR, -1.0f, 1.0f);
    }
}

} // namespace ivanna
