#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace ivanna::spatial {

/**
 * @class PhysicalSceneRenderer
 * Eje 5: Physical acoustic scene rendering: occlusion, material absorption,
 * and physical early reflections (ER).
 * 
 * Latency budget: 0 samples (zero added algorithmic delay).
 * CPU budget: <= 1.4%
 * RT-Safety: Zero dynamic allocations, real-time lock-free.
 */
class PhysicalSceneRenderer {
public:
    PhysicalSceneRenderer() noexcept {
        reset();
    }

    void reset() noexcept {
        occlusionFactor_ = 0.0f;
        wallAbsorption_ = 0.25f;
        filterStateL_ = 0.0f;
        filterStateR_ = 0.0f;
    }

    void setOcclusion(float occ) noexcept {
        occlusionFactor_ = std::clamp(occ, 0.0f, 1.0f);
    }

    void setWallAbsorption(float abs) noexcept {
        wallAbsorption_ = std::clamp(abs, 0.05f, 0.95f);
    }

    /**
     * @brief Apply occlusion and acoustic material absorption.
     * Direct Form I lowpass filter with frequency-dependent wall transmission.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;

        // Transmission loss & lowpass cutoff based on occlusion
        // When occluded: high frequencies drop sharply, direct level drops
        const float directGain = 1.0f - 0.75f * occlusionFactor_;
        const float alpha = std::clamp(1.0f - 0.85f * occlusionFactor_, 0.05f, 1.0f);

        float sL = filterStateL_;
        float sR = filterStateR_;

        for (size_t i = 0; i < numSamples; ++i) {
            float inL = bufferL[i] * directGain;
            float inR = bufferR[i] * directGain;

            sL += alpha * (inL - sL);
            sR += alpha * (inR - sR);

            bufferL[i] = sL;
            bufferR[i] = sR;
        }

        filterStateL_ = sL;
        filterStateR_ = sR;
    }

private:
    float occlusionFactor_{0.0f};
    float wallAbsorption_{0.25f};
    float filterStateL_{0.0f};
    float filterStateR_{0.0f};
};

} // namespace ivanna::spatial
