// © 2026 GORE TNS — IVANNA OMEGA SUPREME. All rights reserved.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ivanna::rt {

// Fixed-cost 64-band Bark observer for the system-wide AudioFlinger callback.
// It is read-only: it never changes PCM, allocates, locks, or calls Android APIs.
// Input is decimated to at most 48 kHz so high-rate USB routes have bounded cost.
class FotoHoloBarkMeter {
public:
    static constexpr std::size_t kBands = 64;

    FotoHoloBarkMeter() noexcept { prepare(48000.0f); }

    void prepare(float inputSampleRate) noexcept {
        inputSampleRate_ = std::isfinite(inputSampleRate) && inputSampleRate >= 8000.0f
            ? inputSampleRate : 48000.0f;
        stride_ = static_cast<std::size_t>(std::max(1.0f, std::ceil(inputSampleRate_ / 48000.0f)));
        // Filters run at the effective post-decimation rate. This keeps the Bark
        // centers correct for 44.1 kHz devices while 96/192 kHz routes decimate to 48 kHz.
        const float sr = inputSampleRate_ / static_cast<float>(stride_);
        for (std::size_t i = 0; i < kBands; ++i) {
            const float bark = 0.2f + (23.5f * static_cast<float>(i) / 63.0f);
            const float center = std::clamp(600.0f * std::sinh(bark / 6.0f), 25.0f, 0.44f * sr);
            const float prevBark = std::max(0.05f, bark - 23.5f / 63.0f);
            const float nextBark = std::min(24.0f, bark + 23.5f / 63.0f);
            const float low = std::max(20.0f, 600.0f * std::sinh(prevBark / 6.0f));
            const float high = std::min(0.46f * sr, 600.0f * std::sinh(nextBark / 6.0f));
            const float bandwidth = std::max(12.0f, high - low);
            configure(filters_[i], center, std::clamp(center / bandwidth, 0.5f, 12.0f), sr);
        }
        reset();
    }

    void reset() noexcept {
        for (auto& f : filters_) f.z1 = f.z2 = 0.0f;
        for (float& v : previous_) v = 0.0f;
        lastDelta_ = 0.0f;
    }

    // Returns a bounded ±0.25 width delta for the NEXT DSP callback.
    // Invalid/silent blocks return zero and cannot leave a stale modulation latched.
    float processBlock(const float* left, const float* right, std::size_t frames,
                       bool enabled, float intensity) noexcept {
        const float k = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
        if (!enabled || k <= 0.0f || !left || !right || frames == 0) {
            // Clear filter history as well as the output so re-enable never inherits stale PCM.
            reset();
            return lastDelta_;
        }
        float energy[kBands]{};
        std::size_t samples = 0;
        for (std::size_t n = 0; n < frames; n += stride_) {
            const float l = std::isfinite(left[n]) ? left[n] : 0.0f;
            const float r = std::isfinite(right[n]) ? right[n] : 0.0f;
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r);
            const float x = std::fabs(mid) >= std::fabs(side) ? mid : side;
            for (std::size_t b = 0; b < kBands; ++b) {
                const float y = filters_[b].process(x);
                energy[b] += y * y;
            }
            ++samples;
        }
        if (samples == 0) {
            lastDelta_ = 0.0f;
            return lastDelta_;
        }
        float peak = 0.0f;
        float amplitudes[kBands]{};
        for (std::size_t b = 0; b < kBands; ++b) {
            amplitudes[b] = std::sqrt(std::max(0.0f, energy[b] / static_cast<float>(samples)));
            peak = std::max(peak, amplitudes[b]);
        }
        if (!std::isfinite(peak) || peak < 1.0e-5f) {
            lastDelta_ = 0.0f;
            for (float& v : previous_) v = 0.0f;
            return lastDelta_;
        }
        float sum = 0.0f, low = 0.0f, high = 0.0f, rise = 0.0f;
        for (std::size_t b = 0; b < kBands; ++b) {
            const float v = std::clamp(amplitudes[b] / peak, 0.0f, 1.0f);
            sum += v;
            if (b < 16) low += v;
            if (b >= 40) high += v;
            if (v > previous_[b]) rise += v - previous_[b];
            previous_[b] = v;
        }
        if (sum < 1.0e-3f) {
            lastDelta_ = 0.0f;
            return lastDelta_;
        }
        const float lowShare = low / sum;
        const float highShare = high / sum;
        const float flux = std::clamp(rise / sum, 0.0f, 1.0f);
        const float transientStability = 1.0f - 0.35f * flux;
        lastDelta_ = std::clamp(0.25f * k * (highShare - lowShare) * transientStability,
                                -0.25f, 0.25f);
        return lastDelta_;
    }

    float lastDelta() const noexcept { return lastDelta_; }
    std::size_t decimationStride() const noexcept { return stride_; }

private:
    struct Biquad {
        float b0{0.f}, b1{0.f}, b2{0.f}, a1{0.f}, a2{0.f}, z1{0.f}, z2{0.f};
        float process(float x) noexcept {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            if (!std::isfinite(y)) { z1 = z2 = 0.0f; return 0.0f; }
            return y;
        }
    };

    static void configure(Biquad& q, float center, float quality, float sr) noexcept {
        const float w = 6.2831853071795864769f * center / sr;
        const float sn = std::sin(w), cs = std::cos(w);
        const float alpha = sn / (2.0f * quality);
        const float a0 = 1.0f + alpha;
        q.b0 = alpha / a0; q.b1 = 0.0f; q.b2 = -alpha / a0;
        q.a1 = -2.0f * cs / a0; q.a2 = (1.0f - alpha) / a0;
        q.z1 = q.z2 = 0.0f;
    }

    Biquad filters_[kBands]{};
    float previous_[kBands]{};
    float inputSampleRate_{48000.0f};
    std::size_t stride_{1};
    float lastDelta_{0.0f};
};

} // namespace ivanna::rt
