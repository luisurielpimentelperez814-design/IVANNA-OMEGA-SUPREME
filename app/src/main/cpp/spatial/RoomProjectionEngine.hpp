#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "RirConvolver.hpp"
#include "../supreme/SupremeTransitionEnvelope.hpp"

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
        inversionEnv_.configure(48000.0f, 8.0f, 18.0f, 35.0f);
        inversionEnv_.setImmediate(0.25f);
    }

    void setInversionGain(float gain) noexcept {
        const float clamped = std::clamp(gain, 0.0f, 1.0f);
        inversionGain_.store(clamped, std::memory_order_relaxed);
        if (inversionEnv_.renderedBlocks == 0u) {
            inversionEnv_.setImmediate(clamped);
        }
    }

    void setProjectionWet(float wet) noexcept {
        projectionWet_.store(std::clamp(wet, 0.0f, 1.0f), std::memory_order_relaxed);
        convolver_.setWetDry(wet);
    }

    void reset() noexcept {
        envStateL_ = 0.0f;
        envStateR_ = 0.0f;
        envFastL_  = 0.0f;
        envFastR_  = 0.0f;
        wpeHistL_.fill(0.0f);
        wpeHistR_.fill(0.0f);
        wpeWriteIdx_ = 0;
        inversionEnv_.setImmediate(inversionGain_.load(std::memory_order_relaxed));
    }

    /**
     * @brief Process stereo block through partial inversion and virtual room projection.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;

        const float invGain = inversionGain_.load(std::memory_order_relaxed);

        // 1. De-Reverberación de Fase Mínima por Predicción Lineal Ponderada (WPE) libre de divisiones
        //    Protegida por SupremeTransitionEnvelope para evitar clics al activar/desactivar WPE.
        if (inversionEnv_.beginBlock(invGain)) {
            float envFastL = envFastL_, envSlowL = envStateL_;
            float envFastR = envFastR_, envSlowR = envStateR_;
            int wIdx = wpeWriteIdx_;

            for (size_t i = 0; i < numSamples; ++i) {
                const float gInv = inversionEnv_.nextSample();
                const float xL = bufferL[i];
                const float xR = bufferR[i];
                const float absL = std::fabs(xL);
                const float absR = std::fabs(xR);

                envFastL += 0.12f * (absL - envFastL);
                envSlowL += 0.01f * (absL - envSlowL);
                envFastR += 0.12f * (absR - envFastR);
                envSlowR += 0.01f * (absR - envSlowR);

                // Predicción lineal WPE de 4 taps desde el historial retardado (Δ = 16 muestras)
                const int d0 = (wIdx + 16) & 31;
                const int d1 = (wIdx + 19) & 31;
                const int d2 = (wIdx + 23) & 31;
                const int d3 = (wIdx + 28) & 31;

                const float predLateL = wpeTap_[0] * wpeHistL_[d0] + wpeTap_[1] * wpeHistL_[d1]
                                      + wpeTap_[2] * wpeHistL_[d2] + wpeTap_[3] * wpeHistL_[d3];
                const float predLateR = wpeTap_[0] * wpeHistR_[d0] + wpeTap_[1] * wpeHistR_[d1]
                                      + wpeTap_[2] * wpeHistR_[d2] + wpeTap_[3] * wpeHistR_[d3];

                wpeWriteIdx_ = wIdx = (wIdx - 1) & 31;
                wpeHistL_[wIdx] = xL;
                wpeHistR_[wIdx] = xR;

                // Guardia de transientes: cuando envFast > envSlow (ataque directo), la sustracción WPE se inhibe
                const float tailRatioL = std::clamp(envSlowL - 0.65f * envFastL, 0.0f, 0.45f);
                const float tailRatioR = std::clamp(envSlowR - 0.65f * envFastR, 0.0f, 0.45f);

                const float cleanL = xL - (gInv * 0.28f) * predLateL;
                const float cleanR = xR - (gInv * 0.28f) * predLateR;

                const float cepGainL = std::max(0.62f, 1.0f - gInv * tailRatioL);
                const float cepGainR = std::max(0.62f, 1.0f - gInv * tailRatioR);

                bufferL[i] = cleanL * cepGainL;
                bufferR[i] = cleanR * cepGainR;
            }
            envFastL_  = envFastL; envStateL_ = envSlowL;
            envFastR_  = envFastR; envStateR_ = envSlowR;
            if (inversionEnv_.isSilent()) {
                envStateL_ = envStateR_ = envFastL_ = envFastR_ = 0.0f;
                wpeHistL_.fill(0.0f);
                wpeHistR_.fill(0.0f);
                wpeWriteIdx_ = 0;
            }
        }

        // 2. Virtual Room Projection via RirConvolver (su propio wetNow_ hace rampa suave a 0)
        if (convolver_.isLoaded()) {
            convolver_.process(bufferL, bufferR, static_cast<int>(numSamples));
        }
    }

    Ivanna::RirConvolver& getConvolver() noexcept {
        return convolver_;
    }

private:
    Ivanna::RirConvolver convolver_;
    std::atomic<float> inversionGain_{0.25f};
    std::atomic<float> projectionWet_{0.35f};

    float envStateL_{0.0f};
    float envStateR_{0.0f};
    float envFastL_{0.0f};
    float envFastR_{0.0f};
    alignas(64) std::array<float, 32> wpeHistL_{};
    alignas(64) std::array<float, 32> wpeHistR_{};
    int wpeWriteIdx_{0};
    std::array<float, 4> wpeTap_{0.42f, 0.28f, 0.18f, 0.12f};
    ivanna::supreme::SupremeTransitionEnvelope inversionEnv_{};
};

} // namespace ivanna::spatial
