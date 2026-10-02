#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include "../supreme/SupremeZeroPopTransition.hpp"
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
            std::max(d0 + 0.35f, 2.0f * (Ly - y0) - Ds),
            2.0f * y0 + Ds,
            std::hypot(Ds, z0 + zs),
            std::hypot(Ds, std::max(0.5f, 2.0f * Lz - zs - z0))
        };
        const float az[kTaps] = {
            -std::atan2(Lx, Ds),
            +std::atan2(Lx, Ds),
            0.0f,
            3.14159265f,
            -0.15f,
            +0.15f
        };

        for (int k = 0; k < kTaps; ++k) {
            const float dk = std::max(d[k], d0 + 0.20f);
            const int rawTap = static_cast<int>(std::lround(((dk - d0) / 343.0f) * fs_));
            tap_[k] = std::clamp(rawTap, 8, kBuf - 2);
            const float g = Rw * (d0 / dk);
            gL_[k] = g * (1.0f - 0.35f * std::sin(az[k]));
            gR_[k] = g * (1.0f + 0.35f * std::sin(az[k]));
        }
        aLp_ = 1.0f - std::exp(-6.283185307179586f * 4500.0f / fs_);
    }

    void processSample(float inMono, float& outL, float& outR) noexcept {
        buf_[w_] = std::isfinite(inMono) ? inMono : 0.0f;
        float l = 0.0f;
        float r = 0.0f;
        for (int k = 0; k < kTaps; ++k) {
            const float s = buf_[(w_ - tap_[k]) & (kBuf - 1)];
            lp_[k] += aLp_ * (s - lp_[k]);
            l += gL_[k] * lp_[k];
            r += gR_[k] * lp_[k];
        }
        w_ = (w_ + 1) & (kBuf - 1);
        outL = l;
        outR = r;
    }

    int   tapSamples(int k) const noexcept { return (k >= 0 && k < kTaps) ? tap_[k] : 0; }
    float gainL(int k)      const noexcept { return (k >= 0 && k < kTaps) ? gL_[k]  : 0.0f; }
    float gainR(int k)      const noexcept { return (k >= 0 && k < kTaps) ? gR_[k]  : 0.0f; }
    float sampleRate()      const noexcept { return fs_; }

private:
    float fs_  = 48000.0f;
    float aLp_ = 0.44f;
    int   w_   = 0;
    std::array<int, kTaps>   tap_{192, 192, 420, 560, 120, 240};
    std::array<float, kTaps> gL_{0.25f, 0.14f, 0.16f, 0.12f, 0.22f, 0.18f};
    std::array<float, kTaps> gR_{0.14f, 0.25f, 0.16f, 0.12f, 0.20f, 0.20f};
    std::array<float, kTaps> lp_{};
    std::array<float, kBuf>  buf_{};
};

// ============================================================================
// DereverbPreFilter (Fallback Legacy R3)
// ============================================================================
class DereverbPreFilter {
public:
    void reset() noexcept {
        fastEnvL_ = 0.0f;
        slowEnvL_ = 0.0f;
        fastEnvR_ = 0.0f;
        slowEnvR_ = 0.0f;
    }

    void processSample(float& l, float& r, float strength) noexcept {
        strength = std::clamp(strength, 0.0f, 0.85f);
        const float absL = std::fabs(l);
        const float absR = std::fabs(r);

        fastEnvL_ = 0.92f * fastEnvL_ + 0.08f * absL;
        slowEnvL_ = 0.996f * slowEnvL_ + 0.004f * absL;
        fastEnvR_ = 0.92f * fastEnvR_ + 0.08f * absR;
        slowEnvR_ = 0.996f * slowEnvR_ + 0.004f * absR;

        const float tailRatioL = std::clamp((slowEnvL_ - fastEnvL_) / (slowEnvL_ + 1e-6f), 0.0f, 1.0f);
        const float tailRatioR = std::clamp((slowEnvR_ - fastEnvR_) / (slowEnvR_ + 1e-6f), 0.0f, 1.0f);

        l *= (1.0f - strength * 0.45f * tailRatioL);
        r *= (1.0f - strength * 0.45f * tailRatioR);
    }

private:
    float fastEnvL_{0.0f};
    float slowEnvL_{0.0f};
    float fastEnvR_{0.0f};
    float slowEnvR_{0.0f};
};

// ============================================================================
// EarlyReflectionCluster (Fallback Legacy R3)
// ============================================================================
class EarlyReflectionCluster {
public:
    static constexpr size_t kBufferSize = 2048;

    void reset() noexcept {
        bufferL_.fill(0.0f);
        bufferR_.fill(0.0f);
        writeIdx_ = 0;
    }

    void configureRoomSize(float roomScale) noexcept {
        roomScale = std::clamp(roomScale, 0.4f, 1.8f);
        tapsL_[0] = static_cast<size_t>(std::clamp(180.0f * roomScale, 32.0f, 1900.0f));
        tapsL_[1] = static_cast<size_t>(std::clamp(410.0f * roomScale, 64.0f, 1900.0f));
        tapsL_[2] = static_cast<size_t>(std::clamp(790.0f * roomScale, 96.0f, 1900.0f));

        tapsR_[0] = static_cast<size_t>(std::clamp(220.0f * roomScale, 32.0f, 1900.0f));
        tapsR_[1] = static_cast<size_t>(std::clamp(470.0f * roomScale, 64.0f, 1900.0f));
        tapsR_[2] = static_cast<size_t>(std::clamp(860.0f * roomScale, 96.0f, 1900.0f));
    }

    void processSample(float inL, float inR, float& reflL, float& reflR) noexcept {
        bufferL_[writeIdx_] = inL;
        bufferR_[writeIdx_] = inR;

        const float l0 = bufferL_[(writeIdx_ + kBufferSize - tapsL_[0]) & (kBufferSize - 1)];
        const float l1 = bufferR_[(writeIdx_ + kBufferSize - tapsL_[1]) & (kBufferSize - 1)];
        const float l2 = bufferL_[(writeIdx_ + kBufferSize - tapsL_[2]) & (kBufferSize - 1)];

        const float r0 = bufferR_[(writeIdx_ + kBufferSize - tapsR_[0]) & (kBufferSize - 1)];
        const float r1 = bufferL_[(writeIdx_ + kBufferSize - tapsR_[1]) & (kBufferSize - 1)];
        const float r2 = bufferR_[(writeIdx_ + kBufferSize - tapsR_[2]) & (kBufferSize - 1)];

        reflL = 0.42f * l0 + 0.28f * l1 + 0.16f * l2;
        reflR = 0.42f * r0 + 0.28f * r1 + 0.16f * r2;

        writeIdx_ = (writeIdx_ + 1) & (kBufferSize - 1);
    }

private:
    std::array<float, kBufferSize> bufferL_{};
    std::array<float, kBufferSize> bufferR_{};
    std::array<size_t, 3> tapsL_{180, 410, 790};
    std::array<size_t, 3> tapsR_{220, 470, 860};
    size_t writeIdx_{0};
};

// ============================================================================
// RoomProjectionEngine — Motor Unificado de De-Reverberación (M5) y
// Proyección de Reflexiones Tempranas Físicas (M7)
// ============================================================================
class RoomProjectionEngine {
public:
    RoomProjectionEngine() noexcept {
        prepare(48000.0f);
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        lateReverbSuppressor_.prepare(sampleRate_);
        physicalEr_.prepare(sampleRate_);
        physicalEr_.setRoom(roomLx_, roomLy_, roomLz_, roomRt60_);
        inversionEnv_.prepare(sampleRate_, 5.0f);
    }

    void reset() noexcept {
        continuityMgr_.onResetRequested(true);
        inversionEnv_.prepare(sampleRate_, 5.0f);
        for (size_t i = 0; i < wpeHistoryL_.size(); ++i) {
            if (!std::isfinite(wpeHistoryL_[i])) wpeHistoryL_[i] = 0.0f;
            if (!std::isfinite(wpeHistoryR_[i])) wpeHistoryR_[i] = 0.0f;
        }
    }

    void hardClearState() noexcept {
        dereverb_.reset();
        lateReverbSuppressor_.reset();
        reflections_.reset();
        physicalEr_.reset();
        wpeHistoryL_.fill(0.0f);
        wpeHistoryR_.fill(0.0f);
        wpeWriteIdx_ = 0;
        inversionEnv_.prepare(sampleRate_, 5.0f);
        continuityMgr_.markHardCleared();
    }

    void setInversionGain(float gain) noexcept {
        if (!std::isfinite(gain)) return;
        inversionGain_.store(std::clamp(gain, 0.0f, 0.85f), std::memory_order_relaxed);
    }
    float getInversionGain() const noexcept {
        return inversionGain_.load(std::memory_order_relaxed);
    }

    void setProjectionWet(float wet) noexcept {
        if (!std::isfinite(wet)) return;
        projectionWet_.store(std::clamp(wet, 0.0f, 0.65f), std::memory_order_relaxed);
    }
    float getProjectionWet() const noexcept {
        return projectionWet_.load(std::memory_order_relaxed);
    }

    void setRoomScale(float scale) noexcept {
        reflections_.configureRoomSize(scale);
        const float s = std::clamp(scale, 0.5f, 2.0f);
        setRoomGeometry(6.0f * s, 8.0f * s, 3.0f * (0.75f + 0.25f * s), 0.35f * s);
    }

    // M6/M7: Geometría física de sala derivada de wfsSpread, hrtfDepth y envDepth
    void setRoomGeometry(float Lx, float Ly, float Lz, float rt60) noexcept {
        roomLx_   = std::clamp(Lx,   2.5f, 25.0f);
        roomLy_   = std::clamp(Ly,   2.5f, 30.0f);
        roomLz_   = std::clamp(Lz,   2.2f, 12.0f);
        roomRt60_ = std::clamp(rt60, 0.15f, 3.5f);
        physicalEr_.setRoom(roomLx_, roomLy_, roomLz_, roomRt60_);
    }

    void setEstimatedRoomT60(float rt60Sec) noexcept {
        if (!std::isfinite(rt60Sec)) return;
        roomRt60_ = std::clamp(rt60Sec, 0.12f, 2.50f);
        physicalEr_.setRoom(roomLx_, roomLy_, roomLz_, roomRt60_);
    }

    float estimatedRoomT60() const noexcept {
        return roomRt60_;
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

    void process(float* left, float* right, size_t numSamples) noexcept {
        if (!left || !right || numSamples == 0) return;

        const float targetInv = inversionGain_.load(std::memory_order_relaxed);
        const float wet = projectionWet_.load(std::memory_order_relaxed);
        const bool statDereverb = useStatDereverb_.load(std::memory_order_relaxed);
        const bool physEr = usePhysicalEr_.load(std::memory_order_relaxed);

        inversionEnv_.setTarget(targetInv > 1.0e-4f ? 1.0f : 0.0f);
        continuityMgr_.updateState(targetInv > 1.0e-4f || wet > 1.0e-4f, inversionEnv_.current());

        // Paso 1: De-reverberación estadística de cola difusa (M5: Lebart/Habets)
        if (statDereverb && targetInv > 1.0e-4f) {
            lateReverbSuppressor_.process(left, right, numSamples, targetInv);
        }

        float stateEnergy = 0.0f;
        for (size_t i = 0; i < numSamples; ++i) {
            const float env = inversionEnv_.next();
            const float inv = targetInv * env;

            // Mantener siempre caliente la línea de predicción WPE para continuidad acústica
            // e invariantes de preservedStateEnergy() en cambios de estado en vivo
            const int d1 = (wpeWriteIdx_ - 24 + kWpeDelayLen) & (kWpeDelayLen - 1);
            const int d2 = (wpeWriteIdx_ - 36 + kWpeDelayLen) & (kWpeDelayLen - 1);
            const int d3 = (wpeWriteIdx_ - 52 + kWpeDelayLen) & (kWpeDelayLen - 1);
            const int d4 = (wpeWriteIdx_ - 72 + kWpeDelayLen) & (kWpeDelayLen - 1);

            const float predL = wpeTap_[0] * wpeHistoryL_[d1] + wpeTap_[1] * wpeHistoryL_[d2]
                              + wpeTap_[2] * wpeHistoryL_[d3] + wpeTap_[3] * wpeHistoryL_[d4];
            const float predR = wpeTap_[0] * wpeHistoryR_[d1] + wpeTap_[1] * wpeHistoryR_[d2]
                              + wpeTap_[2] * wpeHistoryR_[d3] + wpeTap_[3] * wpeHistoryR_[d4];

            const float errL = left[i] - inv * 0.38f * predL;
            const float errR = right[i] - inv * 0.38f * predR;

            const float normL = 1.0f / (1.0e-3f + wpeHistoryL_[d1] * wpeHistoryL_[d1] + wpeHistoryL_[d2] * wpeHistoryL_[d2]);
            const float mu = 0.0015f * inv * normL;
            wpeTap_[0] = std::clamp(wpeTap_[0] * 0.9995f + mu * errL * wpeHistoryL_[d1], -0.45f, 0.45f);
            wpeTap_[1] = std::clamp(wpeTap_[1] * 0.9995f + mu * errL * wpeHistoryL_[d2], -0.35f, 0.35f);

            wpeHistoryL_[wpeWriteIdx_] = left[i];
            wpeHistoryR_[wpeWriteIdx_] = right[i];
            wpeWriteIdx_ = (wpeWriteIdx_ + 1) & (kWpeDelayLen - 1);

            if (!statDereverb) {
                left[i] = errL;
                right[i] = errR;
                dereverb_.processSample(left[i], right[i], inv);
            }

            float reflL = 0.0f;
            float reflR = 0.0f;
            if (physEr) {
                const float monoIn = 0.5f * (left[i] + right[i]);
                physicalEr_.processSample(monoIn, reflL, reflR);
            } else {
                reflections_.processSample(left[i], right[i], reflL, reflR);
            }

            left[i]  = (1.0f - wet * 0.35f) * left[i]  + wet * reflL;
            right[i] = (1.0f - wet * 0.35f) * right[i] + wet * reflR;
            stateEnergy += wpeHistoryL_[d1] * wpeHistoryL_[d1] + wpeHistoryR_[d1] * wpeHistoryR_[d1];
        }
        continuityMgr_.recordStateEnergy(stateEnergy / static_cast<float>(numSamples));
    }

    float preservedStateEnergy() const noexcept { return continuityMgr_.preservedStateEnergy(); }

private:
    static constexpr int kWpeDelayLen = 128;
    float sampleRate_{48000.0f};
    float roomLx_{6.0f};
    float roomLy_{8.0f};
    float roomLz_{3.0f};
    float roomRt60_{0.45f};

    LateReverbSuppressor lateReverbSuppressor_{};
    PhysicalEarlyReflections physicalEr_{};
    DereverbPreFilter dereverb_{};
    EarlyReflectionCluster reflections_{};

    std::atomic<float> inversionGain_{0.25f};
    std::atomic<float> projectionWet_{0.18f};
    std::atomic<bool>  useStatDereverb_{true};
    std::atomic<bool>  usePhysicalEr_{true};

    std::array<float, kWpeDelayLen> wpeHistoryL_{};
    std::array<float, kWpeDelayLen> wpeHistoryR_{};
    int wpeWriteIdx_{0};
    std::array<float, 4> wpeTap_{0.42f, 0.28f, 0.18f, 0.12f};
    ivanna::supreme::SupremeTransitionEnvelope inversionEnv_{};
    ivanna::supreme::SupremeStateContinuityManager continuityMgr_{};
};

} // namespace ivanna::spatial
