#pragma once
/**
 * HarmonicExciter.h — Psychoacoustic harmonic saturation with 2x oversampling
 *
 * Processing pipeline per channel:
 *   1. High-pass filter at 2.8 kHz (butterworth 2nd order) — only excite highs
 *   2. 2x upsample via 16-tap polyphase half-band FIR
 *   3. Shaper mode 1 (default, M9): Chebyshev T2 (even) + T3 (odd) harmonic shaper
 *      governed by warmth and spectral flatness (Anti-IMD) + 15 Hz DC blocker.
 *      Shaper mode 0 (legacy fallback, R3): Padé [3/2] rational tanh soft-clip.
 *   4. 2x downsample via matched polyphase FIR (rejects images > fs/2)
 *   5. Mix excited signal back with dry signal via wet parameter
 *
 * Copyright (C) 2026 IVANNA-OMEGA Project
 */

#include "dsp_types.h"
#include "../dsp/ChebHarmonicShaper.hpp"
#include <cstddef>

namespace ivanna {

class HarmonicExciter {
public:
    HarmonicExciter();

    void setParams(const DSPParams& params);
    void process(float* left, float* right, size_t numFrames);
    void reset();

    // Controles Atlas-Escena (M9 / R3):
    // mode = 1: Chebyshev T2+T3 (default), mode = 0: softClip Padé legacy
    void setShaperMode(int mode) noexcept { shaperMode_ = (mode == 0) ? 0 : 1; }
    int  shaperMode() const noexcept { return shaperMode_; }

    void setWarmth(float warmth) noexcept;
    float warmth() const noexcept { return warmth_; }

    void setFlatness1m(float f8) noexcept;
    float flatness1m() const noexcept { return flatness1m_; }

private:
    float drive_      = 1.5f;   // internal gain before shaper (1.0 – 16.0)
    float driveNorm_  = 0.2f;   // normalized drive [0.0 – 1.0] for ChebHarmonicShaper
    float wet_        = 0.15f;  // mix ratio (0.0 – 1.0)
    float excScale_   = 0.30f;  // internal ceiling for excited path
    float sampleRate_ = 48000.0f;
    int   shaperMode_ = 1;      // 1 = Chebyshev M9 (default), 0 = Padé legacy
    float warmth_     = 0.5f;   // [0..1] par/impar balance
    float flatness1m_ = 0.25f;  // [0..1] anti-IMD spectral flatness proxy

    // 2nd-order Butterworth HPF at 2.8 kHz (state per channel)
    BiquadCoeffs hpfCoeffs_{};
    BiquadState  hpfStateL_{};
    BiquadState  hpfStateR_{};

    // 2nd-order Butterworth LPF at 0.45 * fs (half-band anti-aliasing)
    BiquadCoeffs lpfCoeffs_{};
    BiquadState  lpfUpL_{},   lpfUpR_{};
    BiquadState  lpfDownL_{}, lpfDownR_{};

    // Polinomios de Chebyshev T2 + T3 con bloqueador DC a 2*fs (oversampled)
    dsp::ChebHarmonicShaper chebL_{};
    dsp::ChebHarmonicShaper chebR_{};

    void computeFilterCoeffs();
    static float softClip(float x) noexcept;
    static float tickBiquad(float x, const BiquadCoeffs& c, BiquadState& s) noexcept;

    void upsample2x(const float* in, float* out, size_t numFrames,
                    BiquadState& lpfState) noexcept;
    void downsample2x(const float* in, float* out, size_t numFrames,
                      BiquadState& lpfState) noexcept;
};

} // namespace ivanna
