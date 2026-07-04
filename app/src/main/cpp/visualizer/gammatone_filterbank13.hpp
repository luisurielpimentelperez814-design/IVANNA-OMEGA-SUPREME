// gammatone_filterbank13.hpp
#pragma once
#include <array>
#include <cmath>
#include <atomic>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IVANNA_GAMMATONE_HAS_NEON 1
#else
#define IVANNA_GAMMATONE_HAS_NEON 0
#endif

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
    float a1 = 0.f, a2 = 0.f;
    float gain = 1.f;

    // GT_ORDER secciones Direct Form II Transposed en cascada.
    std::array<float, GT_ORDER> s1{}, s2{};

    void design(float centerFreqHz, float fs) noexcept {
        fc = centerFreqHz;
        bw = erbBandwidth(fc);

        const float T = 1.0f / fs;
        const float b  = 2.0f * (float)M_PI * bw;
        const float w0 = 2.0f * (float)M_PI * fc;

        const float e_bT = expf(-b * T);

        a1 = -2.0f * cosf(w0 * T) * e_bT;
        a2 = e_bT * e_bT;

        const float denomRe = 1.0f + a1 * cosf(w0 * T) + a2 * cosf(2.0f * w0 * T);
        const float denomIm =        a1 * sinf(w0 * T) + a2 * sinf(2.0f * w0 * T);
        const float magSection = 1.0f / sqrtf(denomRe * denomRe + denomIm * denomIm);
        gain = powf(magSection, -static_cast<float>(GT_ORDER));
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
        int b = 0;
#if IVANNA_GAMMATONE_HAS_NEON
        for (; b + 3 < GT_BANDS; b += 4) {
            process4BandsNeon(b, in, n, out);
        }
#endif
        for (; b < GT_BANDS; ++b) {
            out[b] = channels_[b].processBlockEnergy(in, n);
        }
    }

#if IVANNA_GAMMATONE_HAS_NEON
    inline void process4BandsNeon(int base, const float* __restrict__ in, int n, float out[GT_BANDS]) noexcept {
        float gainV[4], a1V[4], a2V[4];
        float s1V[GT_ORDER][4], s2V[GT_ORDER][4];
        for (int lane = 0; lane < 4; ++lane) {
            auto& ch = channels_[base + lane];
            gainV[lane] = ch.gain;
            a1V[lane] = ch.a1;
            a2V[lane] = ch.a2;
            for (int sec = 0; sec < GT_ORDER; ++sec) {
                s1V[sec][lane] = ch.s1[sec];
                s2V[sec][lane] = ch.s2[sec];
            }
        }

        const float32x4_t gain = vld1q_f32(gainV);
        const float32x4_t a1 = vld1q_f32(a1V);
        const float32x4_t a2 = vld1q_f32(a2V);
        float32x4_t s1[GT_ORDER], s2[GT_ORDER];
        for (int sec = 0; sec < GT_ORDER; ++sec) {
            s1[sec] = vld1q_f32(s1V[sec]);
            s2[sec] = vld1q_f32(s2V[sec]);
        }

        float32x4_t acc = vdupq_n_f32(0.0f);
        for (int i = 0; i < n; ++i) {
            float32x4_t x = vmulq_f32(vdupq_n_f32(in[i]), gain);
            for (int sec = 0; sec < GT_ORDER; ++sec) {
                const float32x4_t y = vaddq_f32(x, s1[sec]);
                s1[sec] = vsubq_f32(s2[sec], vmulq_f32(a1, y));
                s2[sec] = vnegq_f32(vmulq_f32(a2, y));
                x = y;
            }
            acc = vmlaq_f32(acc, x, x);
        }

        float accV[4];
        vst1q_f32(accV, acc);
        for (int lane = 0; lane < 4; ++lane) {
            out[base + lane] = sqrtf(accV[lane] / static_cast<float>(n));
        }
        for (int sec = 0; sec < GT_ORDER; ++sec) {
            vst1q_f32(s1V[sec], s1[sec]);
            vst1q_f32(s2V[sec], s2[sec]);
        }
        for (int lane = 0; lane < 4; ++lane) {
            auto& ch = channels_[base + lane];
            for (int sec = 0; sec < GT_ORDER; ++sec) {
                ch.s1[sec] = s1V[sec][lane];
                ch.s2[sec] = s2V[sec][lane];
            }
        }
    }
#endif

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
