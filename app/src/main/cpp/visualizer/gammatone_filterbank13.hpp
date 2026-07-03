// gammatone_filterbank13.hpp
#pragma once
#include <array>
#include <cmath>
#include <atomic>

namespace ivanna::vis {

static constexpr int GT_BANDS = 13;
static constexpr int GT_ORDER = 4;  // 4 secciones en cascada (Slaney real-valued)

// ERB(f) = 24.7 * (4.37e-3 * f + 1)  — Glasberg & Moore 1990
static inline float erbBandwidth(float fc) noexcept {
    return 24.7f * (4.37e-3f * fc + 1.0f);
}

struct GammatoneChannel {
    float fc = 1000.f;
    float bw = 0.f;

    float a1 = 0.f, a2 = 0.f, a3 = 0.f, a4 = 0.f, a5 = 0.f;
    float gain = 1.f;

    std::array<float, GT_ORDER> s1{}, s2{};

    void design(float centerFreqHz, float fs) noexcept {
        fc = centerFreqHz;
        bw = erbBandwidth(fc);

        const float T = 1.0f / fs;
        const float b  = 2.0f * (float)M_PI * bw;
        const float w0 = 2.0f * (float)M_PI * fc;

        const float e_bT = expf(-b * T);
        const float cos_w0T = cosf(w0 * T);
        const float sin_w0T = sinf(w0 * T);

        const float p1 = -2.0f * cosf(w0 * T) * e_bT;
        const float p2 = e_bT * e_bT;

        a1 = p1; a2 = p2;

        (void)a3; (void)a4; (void)a5; (void)sin_w0T; (void)cos_w0T;

        const float denomRe = 1.0f + a1 * cosf(w0 * T) + a2 * cosf(2.0f * w0 * T);
        const float denomIm =        a1 * sinf(w0 * T) + a2 * sinf(2.0f * w0 * T);
        const float magSection = 1.0f / sqrtf(denomRe * denomRe + denomIm * denomIm);
        gain = powf(magSection, -static_cast<float>(GT_ORDER));
        gain = 1.0f / gain;
    }

    inline float processBlockEnergy(const float* __restrict__ in, int n) noexcept {
        float acc = 0.f;
        for (int i = 0; i < n; ++i) {
            float x = in[i] * gain;
            for (int sec = 0; sec < GT_ORDER; ++sec) {
                const float y = x + s1[sec];
                s1[sec] = s2[sec] - a1 * y;
                s2[sec] = -a2 * y;
                x = y;
            }
            acc += x * x;
        }
        return sqrtf(acc / static_cast<float>(n));
    }
};

class GammatoneFilterBank13 {
public:
    void init(float fs) noexcept {
        fs_ = fs;
        constexpr float fLow = 80.f, fHigh = 16000.f;
        const float erbLow  = hzToErbRate(fLow);
        const float erbHigh = hzToErbRate(fHigh);
        for (int b = 0; b < GT_BANDS; ++b) {
            const float t = static_cast<float>(b) / (GT_BANDS - 1);
            const float erb = erbLow + t * (erbHigh - erbLow);
            channels_[b].design(erbRateToHz(erb), fs_);
        }
    }

    inline void process(const float* __restrict__ in, int n, float out[GT_BANDS]) noexcept {
        for (int b = 0; b < GT_BANDS; ++b) {
            out[b] = channels_[b].processBlockEnergy(in, n);
        }
    }

private:
    static float hzToErbRate(float f) noexcept {
        return 21.4f * log10f(4.37e-3f * f + 1.0f);
    }
    static float erbRateToHz(float e) noexcept {
        return (powf(10.f, e / 21.4f) - 1.0f) / 4.37e-3f;
    }

    float fs_ = 48000.f;
    std::array<GammatoneChannel, GT_BANDS> channels_;
};

} // namespace ivanna::vis
