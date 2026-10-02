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
#include "LateReverbSuppressor.hpp"

namespace ivanna::spatial {

// ============================================================================
// PhysicalEarlyReflections — Trazador de Fuentes Imagen de 1er Orden (M7)
//   6 superficies especulares (Pared Izq, Pared Der, Frontal, Trasera, Suelo, Techo)
//   con coeficiente de reflexión Sabine-Eyring, ley de distancia d0/dk,
//   panorámica binaural ILD azimutal y absorción de alta frecuencia de pared.
// ============================================================================
class PhysicalEarlyReflections {
public:
    static constexpr int kTaps = 6;
    static constexpr int kBuf  = 4096; // Soporta hasta ~85 ms @ 48 kHz (potencia de 2)

    void prepare(float sr) noexcept {
        fs_ = (sr > 8000.0f) ? sr : 48000.0f;
        reset();
        setRoom(6.0f, 8.0f, 3.0f, 0.45f);
    }

    void reset() noexcept {
        buf_.fill(0.0f);
        lp_.fill(0.0f);
        w_ = 0;
    }

    void setRoom(float Lx, float Ly, float Lz, float rt60) noexcept {
        Lx = std::clamp(Lx, 2.5f, 25.0f);
        Ly = std::clamp(Ly, 2.5f, 30.0f);
        Lz = std::clamp(Lz, 2.2f, 12.0f);
        rt60 = std::clamp(rt60, 0.15f, 3.5f);

        const float V = Lx * Ly * Lz;
        const float S = 2.0f * (Lx * Ly + Ly * Lz + Lx * Lz);
        const float Rw = std::clamp(std::exp(-0.161f * V / (S * std::max(rt60, 0.15f))), 0.15f, 0.92f);

        const float y0 = 0.38f * Ly;
        const float z0 = 1.2f;
        const float zs = 1.4f;
        const float Ds = std::clamp(0.28f * Ly, 1.4f, 4.0f);
        const float d0 = Ds;

        // 6 fuentes imagen de 1er orden:
        //   0: Pared Izq   (theta < 0 => gL > gR)
        //   1: Pared Der   (theta > 0 => gR > gL)
        //   2: Pared Front (theta = 0)
        //   3: Pared Tras  (theta = pi)
        //   4: Suelo       (reflexión vertical inferior)
        //   5: Techo       (reflexión vertical superior)
        const float d[kTaps] = {
            std::hypot(Lx, Ds),
            std::hypot(Lx, Ds),
            2.0f * (Ly - y0) - Ds,
            2.0f * y0 + Ds,
            std::hypot(Ds, z0 + zs),
            std::hypot(Ds, 2.0f * Lz - z0 - zs)
        };

        const float theta[kTaps] = {
            -std::atan2(Lx, Ds),
            +std::atan2(Lx, Ds),
            0.0f,
            3.14159265f,
            0.0f,
            0.0f
        };

        aWall_ = 1.0f - std::exp(-6.283185307179586f * 3500.0f / fs_);
        for (int k = 0; k < kTaps; ++k) {
            const float dk = std::max(d[k], d0 + 0.15f);
            const int rawTap = static_cast<int>(std::lround(((dk - d0) / 343.0f) * fs_));
            tap_[k] = std::clamp(rawTap, 1, kBuf - 8);

            const float gk = Rw * (d0 / dk);
            const float pan = 0.5f * std::sin(theta[k]);
            gL_[k] = gk * std::sqrt(std::clamp(0.5f - pan, 0.05f, 0.95f));
            gR_[k] = gk * std::sqrt(std::clamp(0.5f + pan, 0.05f, 0.95f));

            const float absTh = std::fabs(theta[k]);
            const float effTh = (absTh > 1.5707963f) ? (3.14159265f - absTh) : absTh;
            const float itdSec = (0.0875f / 343.0f) * (effTh + std::sin(effTh));
            itdExtra_[k] = std::clamp(static_cast<int>(std::lround(itdSec * fs_)), 0, 32);
        }
    }

    inline void processSample(float monoIn, float& outL, float& outR) noexcept {
        buf_[w_] = monoIn;
        float accL = 0.0f;
        float accR = 0.0f;
        for (int k = 0; k < kTaps; ++k) {
            const int idxBase = (w_ - tap_[k] + kBuf) & (kBuf - 1);
            const float raw = buf_[idxBase];
            lp_[k] += aWall_ * (raw - lp_[k]);
            const float filt = lp_[k];

            if (k == 0) {
                const int idxContra = (idxBase - itdExtra_[k] + kBuf) & (kBuf - 1);
                accL += gL_[k] * filt;
                accR += gR_[k] * (0.7f * filt + 0.3f * buf_[idxContra]);
            } else if (k == 1) {
                const int idxContra = (idxBase - itdExtra_[k] + kBuf) & (kBuf - 1);
                accL += gL_[k] * (0.7f * filt + 0.3f * buf_[idxContra]);
                accR += gR_[k] * filt;
            } else {
                accL += gL_[k] * filt;
                accR += gR_[k] * filt;
            }
        }
        w_ = (w_ + 1) & (kBuf - 1);
        outL = accL;
        outR = accR;
    }

    int   tapSamples(int k) const noexcept { return (k >= 0 && k < kTaps) ? tap_[k] : 0; }
    float gainL(int k) const noexcept      { return (k >= 0 && k < kTaps) ? gL_[k]  : 0.0f; }
    float gainR(int k) const noexcept      { return (k >= 0 && k < kTaps) ? gR_[k]  : 0.0f; }

private:
    float fs_ = 48000.0f;
    float aWall_ = 0.36f;
    std::array<float, kBuf>  buf_{};
    std::array<float, kTaps> lp_{};
    std::array<int,   kTaps> tap_{24, 24, 60, 84, 38, 52};
    std::array<int,   kTaps> itdExtra_{12, 12, 0, 0, 0, 0};
    std::array<float, kTaps> gL_{0.22f, 0.12f, 0.16f, 0.12f, 0.14f, 0.11f};
    std::array<float, kTaps> gR_{0.12f, 0.22f, 0.16f, 0.12f, 0.14f, 0.11f};
    int w_ = 0;
};

/**
 * @class RoomProjectionEngine
 * Eje 3: Supresión estadística de cola difusa (Lebart/Habets M5 + WPE fallback)
 *        + Reflexiones tempranas físicas de 6 paredes (Sabine-Eyring M7) + RirConvolver.
 *
 * Latency budget: 0 samples added latency on direct path.
 * RT-safe, zero allocations on audio callback.
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
        lateReverbSuppressor_.prepare(48000.0f);
        physicalEr_.prepare(48000.0f);
    }

    void prepare(float sr) noexcept {
        sampleRate_ = (std::isfinite(sr) && sr > 8000.0f) ? sr : 48000.0f;
        inversionEnv_.configure(sampleRate_, 8.0f, 18.0f, 35.0f);
        lateReverbSuppressor_.prepare(sampleRate_);
        physicalEr_.prepare(sampleRate_);
        physicalEr_.setRoom(roomLx_, roomLy_, roomLz_, estimatedRoomT60Sec_.load(std::memory_order_relaxed));
    }

    void setInversionGain(float gain) noexcept {
        if (!std::isfinite(gain)) return;
        const float clamped = std::clamp(gain, 0.0f, 1.0f);
        inversionGain_.store(clamped, std::memory_order_relaxed);
        if (inversionEnv_.renderedBlocks == 0u) {
            inversionEnv_.setImmediate(clamped);
        }
    }

    float getInversionGain() const noexcept {
        return inversionGain_.load(std::memory_order_relaxed);
    }

    void setEstimatedRoomT60(float t60Sec) noexcept {
        if (!std::isfinite(t60Sec)) return;
        const float clampedT60 = std::clamp(t60Sec, 0.05f, 5.0f);
        estimatedRoomT60Sec_.store(clampedT60, std::memory_order_relaxed);
        const float reqWet = projectionWet_.load(std::memory_order_relaxed);
        convolver_.setWetDry(RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(reqWet, clampedT60));
        physicalEr_.setRoom(roomLx_, roomLy_, roomLz_, clampedT60);
    }

    [[nodiscard]] float estimatedRoomT60() const noexcept {
        return estimatedRoomT60Sec_.load(std::memory_order_relaxed);
    }

    void setProjectionWet(float wet) noexcept {
        if (!std::isfinite(wet)) return;
        const float clampedWet = std::clamp(wet, 0.0f, 1.0f);
        projectionWet_.store(clampedWet, std::memory_order_relaxed);
        const float roomT60 = estimatedRoomT60Sec_.load(std::memory_order_relaxed);
        convolver_.setWetDry(RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(clampedWet, roomT60));
    }

    float getProjectionWet() const noexcept {
        return projectionWet_.load(std::memory_order_relaxed);
    }

    // M6/M7: Geometría física de sala derivada de wfsSpread, hrtfDepth y envDepth
    void setRoomGeometry(float Lx, float Ly, float Lz, float rt60) noexcept {
        roomLx_ = std::clamp(Lx, 2.5f, 25.0f);
        roomLy_ = std::clamp(Ly, 2.5f, 30.0f);
        roomLz_ = std::clamp(Lz, 2.2f, 12.0f);
        const float clampedRt60 = std::clamp(rt60, 0.15f, 3.5f);
        estimatedRoomT60Sec_.store(clampedRt60, std::memory_order_relaxed);
        physicalEr_.setRoom(roomLx_, roomLy_, roomLz_, clampedRt60);
    }

    void setUseStatDereverb(bool on) noexcept {
        useStatDereverb_.store(on, std::memory_order_relaxed);
    }
    bool useStatDereverb() const noexcept {
        return useStatDereverb_.load(std::memory_order_relaxed);
    }

    void setUsePhysicalEr(bool on) noexcept {
        usePhysicalEr_.store(on, std::memory_order_relaxed);
    }
    bool usePhysicalEr() const noexcept {
        return usePhysicalEr_.load(std::memory_order_relaxed);
    }

    float lateRatio() const noexcept {
        return lateReverbSuppressor_.lateRatio();
    }

    const PhysicalEarlyReflections& physicalEr() const noexcept { return physicalEr_; }
    PhysicalEarlyReflections& physicalEr() noexcept { return physicalEr_; }
    const LateReverbSuppressor& lateSuppressor() const noexcept { return lateReverbSuppressor_; }

    void reset() noexcept {
        inversionEnv_.setImmediate(inversionGain_.load(std::memory_order_relaxed));
        lateReverbSuppressor_.reset();
        physicalEr_.reset();
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
        const float wet     = projectionWet_.load(std::memory_order_relaxed);
        const bool statDereverb = useStatDereverb_.load(std::memory_order_relaxed);
        const bool physEr       = usePhysicalEr_.load(std::memory_order_relaxed);
        const bool wasSilent    = inversionEnv_.isSilent();

        if (statDereverb && inversionEnv_.currentGain > 1.0e-4f) {
            lateReverbSuppressor_.process(bufferL, bufferR, numSamples, inversionEnv_.currentGain);
        }

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

                if (!statDereverb) {
                    const float tailRatioL = std::clamp(envSlowL - 0.65f * envFastL, 0.0f, 0.45f);
                    const float tailRatioR = std::clamp(envSlowR - 0.65f * envFastR, 0.0f, 0.45f);
                    const float cleanL = xL - (gInv * 0.28f) * predLateL;
                    const float cleanR = xR - (gInv * 0.28f) * predLateR;
                    const float cepGainL = std::max(0.62f, 1.0f - gInv * tailRatioL);
                    const float cepGainR = std::max(0.62f, 1.0f - gInv * tailRatioR);
                    bufferL[i] = cleanL * cepGainL;
                    bufferR[i] = cleanR * cepGainR;
                } else {
                    const float cleanL = xL - (gInv * 0.08f) * predLateL;
                    const float cleanR = xR - (gInv * 0.08f) * predLateR;
                    bufferL[i] = cleanL;
                    bufferR[i] = cleanR;
                }
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

        // 2. Virtual Room Projection via RirConvolver or PhysicalEarlyReflections (M7)
        if (convolver_.isLoaded()) {
            convolver_.process(bufferL, bufferR, static_cast<int>(numSamples));
        } else if (physEr && wet > 1.0e-4f) {
            const float erWet = RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(
                wet, estimatedRoomT60Sec_.load(std::memory_order_relaxed)) * 0.35f;
            for (size_t i = 0; i < numSamples; ++i) {
                float reflL = 0.0f, reflR = 0.0f;
                const float mono = 0.5f * (bufferL[i] + bufferR[i]);
                physicalEr_.processSample(mono, reflL, reflR);
                bufferL[i] = bufferL[i] * (1.0f - 0.25f * erWet) + reflL * erWet;
                bufferR[i] = bufferR[i] * (1.0f - 0.25f * erWet) + reflR * erWet;
            }
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
    LateReverbSuppressor lateReverbSuppressor_{};
    PhysicalEarlyReflections physicalEr_{};
    float sampleRate_{48000.0f};
    float roomLx_{6.0f};
    float roomLy_{8.0f};
    float roomLz_{3.0f};
    std::atomic<float> inversionGain_{0.25f};
    std::atomic<float> projectionWet_{0.35f};
    std::atomic<float> estimatedRoomT60Sec_{0.42f};
    std::atomic<bool>  useStatDereverb_{false};
    std::atomic<bool>  usePhysicalEr_{false};
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
