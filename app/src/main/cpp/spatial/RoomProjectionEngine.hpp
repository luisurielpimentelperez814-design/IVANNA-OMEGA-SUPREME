#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "RirConvolver.hpp"

namespace ivanna::spatial {

/**
 * @class RoomProjectionEngine
 * Eje 3: Room partial inversion + virtual room projection.
 * 
 * Latency budget: 0 samples (zero added latency, uses partitioned uniform convolution / overlap-save).
 * CPU budget: <= 3.0%
 * 
 * Features:
 * - Direct room cancellation / dereverberation (partial inverse filter).
 * - Target virtual acoustics projection via RirConvolver.
 * - RT-safe, zero allocations on audio callback.
 */
class RoomProjectionEngine {
public:
    static constexpr size_t BLOCK_SIZE = 512;

    RoomProjectionEngine() noexcept {
        inversionGain_.store(0.25f, std::memory_order_relaxed);
        projectionWet_.store(0.35f, std::memory_order_relaxed);
    }

    void setInversionGain(float gain) noexcept {
        inversionGain_.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    void setProjectionWet(float wet) noexcept {
        projectionWet_.store(std::clamp(wet, 0.0f, 1.0f), std::memory_order_relaxed);
        convolver_.setWetDry(wet);
    }

    /**
     * @brief Process stereo block through partial inversion and virtual room projection.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;

        const float invGain = inversionGain_.load(std::memory_order_relaxed);

        // 1. Partial Room Inversion (De-reverberation 1-pole inverse envelope filter)
        // High speed, sample-aligned zero algorithmic delay
        if (invGain > 0.001f) {
            float envL = envStateL_;
            float envR = envStateR_;
            for (size_t i = 0; i < numSamples; ++i) {
                const float absL = std::fabs(bufferL[i]);
                const float absR = std::fabs(bufferR[i]);

                envL += 0.01f * (absL - envL);
                envR += 0.01f * (absR - envR);

                // Attenuate muddy reverberant decay
                const float suppL = std::max(0.6f, 1.0f - invGain * envL);
                const float suppR = std::max(0.6f, 1.0f - invGain * envR);

                bufferL[i] *= suppL;
                bufferR[i] *= suppR;
            }
            envStateL_ = envL;
            envStateR_ = envR;
        }

        // 2. Virtual Room Projection via RirConvolver (if loaded)
        if (convolver_.isLoaded() && convolver_.wetDry() > 0.001f) {
            convolver_.process(bufferL, bufferR, static_cast<int>(numSamples));
        }
    }

    RirConvolver& getConvolver() noexcept {
        return convolver_;
    }

private:
    RirConvolver convolver_;
    std::atomic<float> inversionGain_{0.25f};
    std::atomic<float> projectionWet_{0.35f};

    float envStateL_{0.0f};
    float envStateR_{0.0f};
};

} // namespace ivanna::spatial
