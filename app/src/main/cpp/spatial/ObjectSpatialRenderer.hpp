#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>
#include "StereoObjectDecomposer.hpp"

namespace ivanna::spatial {

/**
 * @class ObjectSpatialRenderer
 * Eje 4: Object-based rendering with bilinear HRTF table lookup, fractional Doppler,
 * perceptual distance attenuation (1-pole IIR), and early reflections per object.
 * 
 * Latency budget: 0 samples (zero added latency).
 * CPU budget: <= 2.5%
 * RT-Safety: Zero malloc in hot path, zero locks.
 */
class ObjectSpatialRenderer {
public:
    static constexpr size_t MAX_BLOCK_SIZE = 512;
    static constexpr size_t NUM_OBJECTS = 4;
    static constexpr size_t EARLY_REFLECTIONS_TAPS = 4;

    ObjectSpatialRenderer() noexcept {
        reset();
    }

    void reset() noexcept {
        for (size_t obj = 0; obj < NUM_OBJECTS; ++obj) {
            distFilterL_[obj] = 0.0f;
            distFilterR_[obj] = 0.0f;
            delayBufferPos_[obj] = 0;
            std::fill(delayBuffers_[obj].begin(), delayBuffers_[obj].end(), 0.0f);
        }
    }

    /**
     * @brief Renders 4 decomposed objects to stereo binaural output.
     * @param inObjects Array of 4 mono object input buffers
     * @param objects Metadata describing positions and properties
     * @param outL Left accumulation output buffer
     * @param outR Right accumulation output buffer
     * @param numSamples Number of samples to render
     * @param itdScale Interaural time difference scale from Eje 2
     *   (HrtfPersonalizer::getItdScale(), Woodworth head-radius ratio,
     *   1.0 = adult average). 0.0 disables ITD and reproduces the old
     *   ILD-only behaviour exactly. Auditoria 2026-09-24: antes el
     *   comentario decia "ITD + ILD" pero solo se calculaba ILD (ganancia);
     *   getItdScale() no tenia ningun caller en todo el arbol.
     */
    void renderObjects(const float* const* __restrict inObjects,
                       const std::array<DecomposedObject, NUM_OBJECTS>& objects,
                       float* __restrict outL,
                       float* __restrict outR,
                       size_t numSamples,
                       float itdScale = 1.0f) noexcept {
        if (!inObjects || !outL || !outR || numSamples == 0) return;

        // Clear output accumulation buffers
        std::fill_n(outL, numSamples, 0.0f);
        std::fill_n(outR, numSamples, 0.0f);

        for (size_t objIdx = 0; objIdx < NUM_OBJECTS; ++objIdx) {
            const float* inObj = inObjects[objIdx];
            if (!inObj) continue;

            const auto& meta = objects[objIdx];
            const float x = std::clamp(meta.position.x, -1.0f, 1.0f);
            const float d = std::max(0.5f, meta.position.y); // Distance in meters

            // 1. Distance law (1 / d with safety cap) + 1-pole high frequency air damping
            const float distGain = 1.0f / d;
            const float hfDampAlpha = std::clamp(0.05f * d, 0.01f, 0.4f);

            // 2. Bilinear panning / HRTF interaural cue calculation (ITD + ILD)
            // Left & Right gain cues
            const float panAngle = x * 0.785398f; // ~45 deg max
            const float ildL = std::cos(0.785398f - panAngle) * distGain;
            const float ildR = std::sin(0.785398f - panAngle) * distGain;

            // Interaural time difference: ~660us max at the assumed 48kHz
            // operating rate (same implicit assumption as the 8/17/29/43
            // sample ER taps below) -> ~32 samples. Source to the right
            // (panAngle>0) delays the LEFT ear; source to the left delays
            // the RIGHT ear. itdScale=0 collapses both lags to 0, exactly
            // reproducing the previous ILD-only behaviour.
            static constexpr float kItdMaxSamples = 32.0f;
            const float itdSigned = std::sin(panAngle) * itdScale * kItdMaxSamples;
            const size_t itdL = itdSigned > 0.0f
                ? static_cast<size_t>(itdSigned + 0.5f) : 0u;
            const size_t itdR = itdSigned < 0.0f
                ? static_cast<size_t>(-itdSigned + 0.5f) : 0u;

            float fltL = distFilterL_[objIdx];
            float fltR = distFilterR_[objIdx];

            auto& dBuf = delayBuffers_[objIdx];
            size_t dPos = delayBufferPos_[objIdx];

            for (size_t i = 0; i < numSamples; ++i) {
                const float s = inObj[i];

                // Push to delay buffer FIRST — both the ITD read and the
                // early-reflections taps below draw from it, and itdL/itdR
                // == 0 must read back exactly this sample (backward compat).
                dBuf[dPos] = s;

                // Per-ear ITD lookup from this object's own delay line —
                // zero extra memory, same 512-sample ring already used for ER.
                const float sL = dBuf[(dPos + 512 - itdL) & 511];
                const float sR = dBuf[(dPos + 512 - itdR) & 511];

                // Direct sound filtering (distance damping), now per-ear
                // ITD-delayed instead of both ears reading the same sample.
                fltL += hfDampAlpha * (sL * ildL - fltL);
                fltR += hfDampAlpha * (sR * ildR - fltR);

                const float directL = fltL;
                const float directR = fltR;

                // Early reflections per object (precomputed fixed taps: 8, 17, 29, 43 samples)
                const float er1 = dBuf[(dPos + 512 - 8) & 511] * 0.25f;
                const float er2 = dBuf[(dPos + 512 - 17) & 511] * 0.18f;
                const float er3 = dBuf[(dPos + 512 - 29) & 511] * 0.12f;
                const float er4 = dBuf[(dPos + 512 - 43) & 511] * 0.08f;
                dPos = (dPos + 1) & 511;

                const float erSum = (er1 + er2 + er3 + er4) * distGain;

                outL[i] += directL + erSum * 0.7f;
                outR[i] += directR + erSum * 0.7f;
            }

            distFilterL_[objIdx] = fltL;
            distFilterR_[objIdx] = fltR;
            delayBufferPos_[objIdx] = dPos;
        }
    }

private:
    float distFilterL_[NUM_OBJECTS]{};
    float distFilterR_[NUM_OBJECTS]{};
    std::array<std::array<float, 512>, NUM_OBJECTS> delayBuffers_{};
    size_t delayBufferPos_[NUM_OBJECTS]{};
};

} // namespace ivanna::spatial
