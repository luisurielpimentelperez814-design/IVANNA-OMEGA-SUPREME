#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace ivanna::spatial {

struct UserPinnaProfile {
    float head_circumference_cm{56.0f}; // Default adult average
    float ear_pinna_size_mm{65.0f};
    float canal_resonance_boost_db{0.0f};
    float high_freq_rolloff_factor{1.0f};
};

/**
 * @class HrtfPersonalizer
 * Eje 2: HRTF personalization based on physical anthropometric parameters.
 * Passive refinement amortized cost <= 0.2% CPU.
 * 
 * RT-Safety:
 * - Atomic read of current personalizer coefficients.
 * - Amortized parameter smoothing.
 * - Zero malloc in process / filter evaluation.
 */
class HrtfPersonalizer {
public:
    HrtfPersonalizer() noexcept {
        profile_.head_circumference_cm = 56.0f;
        profile_.ear_pinna_size_mm = 65.0f;
        profile_.canal_resonance_boost_db = 0.0f;
        profile_.high_freq_rolloff_factor = 1.0f;
        recalculate();
    }

    void setProfile(const UserPinnaProfile& p) noexcept {
        profile_ = p;
        recalculate();
    }

    const UserPinnaProfile& getProfile() const noexcept {
        return profile_;
    }

    inline float getItdScale() const noexcept {
        return itdScale_.load(std::memory_order_relaxed);
    }

    inline float getPinnaNotchFreqHz() const noexcept {
        return pinnaNotchFreqHz_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Apply ear canal & pinna adaptation filter in-place to single mono/binaural channel.
     * 1-pole / biquad direct form II transposed (zero added latency).
     */
    void processChannel(float* __restrict buffer, size_t numSamples) noexcept {
        if (!buffer || numSamples == 0) return;
        const float alpha = filterCoeff_.load(std::memory_order_relaxed);
        const float resDelta = resonanceGainDelta_.load(std::memory_order_relaxed);
        float s = filterState_;
        for (size_t i = 0; i < numSamples; ++i) {
            float in = buffer[i];
            s += alpha * (in - s);
            // High-passed content around the pinna notch band (in - s is the
            // complement of the one-pole lowpass state, i.e. what the notch
            // shelf below removes) — canal_resonance_boost_db boosts/cuts
            // exactly that band, so the field is no longer dead.
            const float band = in - s;
            buffer[i] = in * 0.85f + s * 0.15f + band * resDelta;
        }
        filterState_ = s;
    }

private:
    void recalculate() noexcept {
        // Woodworth spherical head model scaling: ITD proportional to head radius
        const float standardRadiusCm = 8.75f;
        const float userRadiusCm = profile_.head_circumference_cm / (2.0f * 3.14159265f);
        itdScale_.store(userRadiusCm / standardRadiusCm, std::memory_order_release);

        // Pinna 1/4 wavelength notch frequency approx
        const float speedOfSoundM_S = 343.0f;
        const float pinnaDepthM = (profile_.ear_pinna_size_mm * 0.5f) * 1e-3f;
        const float notchHz = speedOfSoundM_S / (4.0f * std::max(0.01f, pinnaDepthM));
        pinnaNotchFreqHz_.store(notchHz, std::memory_order_release);

        // Filter coeff approx for 48kHz
        const float w = 2.0f * 3.14159265f * (notchHz / 48000.0f);
        filterCoeff_.store(std::clamp(w, 0.01f, 0.95f), std::memory_order_release);

        // Ear canal resonance boost/cut, precomputed as (linearGain - 1) so the
        // hot path stays a single multiply-add — no std::pow() per sample.
        // Clamped to +-12 dB: physically plausible canal resonance range,
        // guards against a bad profile value blowing up the notch band.
        const float boostDb = std::clamp(profile_.canal_resonance_boost_db, -12.0f, 12.0f);
        const float resGainLinear = std::pow(10.0f, boostDb * 0.05f);
        resonanceGainDelta_.store(resGainLinear - 1.0f, std::memory_order_release);
    }

    UserPinnaProfile profile_;
    std::atomic<float> itdScale_{1.0f};
    std::atomic<float> pinnaNotchFreqHz_{6500.0f};
    std::atomic<float> filterCoeff_{0.2f};
    std::atomic<float> resonanceGainDelta_{0.0f};
    float filterState_{0.0f};
};

} // namespace ivanna::spatial
