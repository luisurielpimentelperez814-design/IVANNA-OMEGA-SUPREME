#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "RirConvolver.hpp"
#include "RoomGeometryConfig.hpp"
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
        estimatedRoomT60Sec_.store(0.42f, std::memory_order_relaxed);
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

    void setEstimatedRoomT60(float t60Sec) noexcept {
        if (!std::isfinite(t60Sec)) return;
        const float clampedT60 = std::clamp(t60Sec, 0.05f, 5.0f);
        estimatedRoomT60Sec_.store(clampedT60, std::memory_order_relaxed);
        const float reqWet = projectionWet_.load(std::memory_order_relaxed);
        convolver_.setWetDry(RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(reqWet, clampedT60));
    }

    [[nodiscard]] float estimatedRoomT60() const noexcept {
        return estimatedRoomT60Sec_.load(std::memory_order_relaxed);
    }

    void setProjectionWet(float wet) noexcept {
        const float clampedWet = std::clamp(wet, 0.0f, 1.0f);
        projectionWet_.store(clampedWet, std::memory_order_relaxed);
        const float roomT60 = estimatedRoomT60Sec_.load(std::memory_order_relaxed);
        convolver_.setWetDry(RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(clampedWet, roomT60));
    }

    void reset() noexcept {
        inversionEnv_.setImmediate(inversionGain_.load(std::memory_order_relaxed));
        continuityMgr_.validateStateArray(wpeHistL_);
        continuityMgr_.validateStateArray(wpeHistR_);
        continuityMgr_.state().sanitizeScalar(envStateL_);
        continuityMgr_.state().sanitizeScalar(envStateR_);
        continuityMgr_.state().sanitizeScalar(envFastL_);
        continuityMgr_.state().sanitizeScalar(envFastR_);
        continuityMgr_.validateState();
    }

    void preserveWpeTail(const float* __restrict bufferL,
                         const float* __restrict bufferR,
                         size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;
        const size_t tailCount = std::min<size_t>(numSamples, 32u);
        const size_t startIdx  = numSamples - tailCount;
        int wIdx = wpeWriteIdx_;
        for (size_t i = startIdx; i < numSamples; ++i) {
            wIdx = (wIdx - 1) & 31;
            wpeHistL_[wIdx] = std::isfinite(bufferL[i]) ? bufferL[i] : 0.0f;
            wpeHistR_[wIdx] = std::isfinite(bufferR[i]) ? bufferR[i] : 0.0f;
        }
        wpeWriteIdx_ = wIdx;
        continuityMgr_.preserveState(bufferL, bufferR, numSamples);
    }

    /**
     * @brief Process stereo block through partial inversion and virtual room projection.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;

        const float invGain = inversionGain_.load(std::memory_order_relaxed);
        const bool wasSilent = inversionEnv_.isSilent();

        // 1. De-Reverberación de Fase Mínima por Predicción Lineal Ponderada (WPE) libre de divisiones
        //    Protegida por SupremeTransitionEnvelope + SupremeStateContinuityManager (cero destrucción de historial).
        if (inversionEnv_.beginBlock(invGain)) {
            if (wasSilent) {
                continuityMgr_.resume();
                continuityMgr_.validateStateArray(wpeHistL_);
                continuityMgr_.validateStateArray(wpeHistR_);
            }
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
            continuityMgr_.preserveState(bufferL, bufferR, numSamples);
            if (inversionEnv_.isSilent()) {
                continuityMgr_.suspend(bufferL, bufferR, numSamples);
            }
        } else {
            preserveWpeTail(bufferL, bufferR, numSamples);
            continuityMgr_.suspend(bufferL, bufferR, numSamples);
        }

        // 2. Virtual Room Projection via RirConvolver (su propio wetNow_ hace rampa suave a 0)
        if (convolver_.isLoaded()) {
            convolver_.process(bufferL, bufferR, static_cast<int>(numSamples));
        }
    }

    Ivanna::RirConvolver& getConvolver() noexcept {
        return convolver_;
    }

    const ivanna::supreme::SupremeStateContinuityManager& continuityManager() const noexcept {
        return continuityMgr_;
    }

    float preservedStateEnergy() const noexcept {
        float energy = envStateL_ * envStateL_ + envStateR_ * envStateR_
                     + envFastL_ * envFastL_ + envFastR_ * envFastR_;
        for (size_t i = 0; i < wpeHistL_.size(); ++i) {
            energy += wpeHistL_[i] * wpeHistL_[i] + wpeHistR_[i] * wpeHistR_[i];
        }
        return energy;
    }

private:
    Ivanna::RirConvolver convolver_;
    std::atomic<float> inversionGain_{0.25f};
    std::atomic<float> projectionWet_{0.35f};
    std::atomic<float> estimatedRoomT60Sec_{0.42f};

    float envStateL_{0.0f};
    float envStateR_{0.0f};
    float envFastL_{0.0f};
    float envFastR_{0.0f};
    alignas(64) std::array<float, 32> wpeHistL_{};
    alignas(64) std::array<float, 32> wpeHistR_{};
    int wpeWriteIdx_{0};
    std::array<float, 4> wpeTap_{0.42f, 0.28f, 0.18f, 0.12f};
    ivanna::supreme::SupremeTransitionEnvelope inversionEnv_{};
    ivanna::supreme::SupremeStateContinuityManager continuityMgr_{};
};

} // namespace ivanna::spatial
