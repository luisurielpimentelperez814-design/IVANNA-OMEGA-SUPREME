#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ivanna::dsp {

// Applies a bounded, linear per-sample gain ramp to an interleaved-in-time
// stereo pair. RT-safe: no allocation, locks, syscalls, or hidden state.
// The caller retains the returned target as the next block's start gain.
inline float applyStereoGainRamp(float* left, float* right, std::size_t frames,
                                 float startGain, float targetGain) noexcept {
    const auto safeGain = [](float value) noexcept {
        return std::isfinite(value) ? std::clamp(value, 0.50f, 1.0f) : 1.0f;
    };
    startGain = safeGain(startGain);
    targetGain = safeGain(targetGain);
    if (!left || !right || frames == 0u) return targetGain;

    const float step = (targetGain - startGain) / static_cast<float>(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const float gain = startGain + step * static_cast<float>(i + 1u);
        left[i] *= gain;
        right[i] *= gain;
    }
    return targetGain;
}

} // namespace ivanna::dsp
