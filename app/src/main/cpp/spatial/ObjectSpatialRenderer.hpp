#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>
#include "StereoObjectDecomposer.hpp"
#include "RoomGeometryConfig.hpp"

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

    // Constantes físicas nombradas (§0.5, §4, §7.5):
    static constexpr float kDefaultSampleRateHz = 48000.0f;
    static constexpr float kWoodworthMaxItdSec  = 0.0006666667f; // ~667 us (radio cefálico Woodworth ~8.75 cm)
    static constexpr float kItdMaxSamples       = kWoodworthMaxItdSec * kDefaultSampleRateHz; // 32.0 muestras @ 48 kHz
    static constexpr float kParamSmoothTauSec   = 0.015f; // Constante de tiempo τ = 15 ms (patrón anti-click WFS §7.5)
    static constexpr float kQuarterPiRad        = 0.7853981633974483096f; // pi/4 rad (ley de paneo constante ±45°)
    static constexpr float kErMixScale          = 0.70f; // Peso relativo de reflexiones tempranas frente a campo directo

    ObjectSpatialRenderer() noexcept {
        reset();
    }

    void reset() noexcept {
        for (size_t obj = 0; obj < NUM_OBJECTS; ++obj) {
            distFilterL_[obj] = 0.0f;
            distFilterR_[obj] = 0.0f;
            delayBufferPos_[obj] = 0;
            ildSmoothL_[obj] = 0.0f;
            ildSmoothR_[obj] = 0.0f;
            itdSmoothL_[obj] = 0.0f;
            itdSmoothR_[obj] = 0.0f;
            tapSmoothInit_[obj] = false;
            std::fill(delayBuffers_[obj].begin(), delayBuffers_[obj].end(), 0.0f);
        }
        erGains_ = {0.25f, 0.18f, 0.12f, 0.08f};
        estimatedRoomT60Sec_ = 0.34f;
    }

    void setEarlyReflectionGains(const std::array<float, EARLY_REFLECTIONS_TAPS>& gains) noexcept {
        for (size_t k = 0; k < EARLY_REFLECTIONS_TAPS; ++k) {
            erGains_[k] = std::clamp(gains[k], 0.0f, 0.45f);
        }
    }

    // §6.2 & §7.7: En sala viva (T60 >= 1.2 s) las reflexiones tempranas (ER) sintéticas se apagan (0.0).
    void setEstimatedRoomT60(float t60Sec) noexcept {
        if (!std::isfinite(t60Sec)) return;
        estimatedRoomT60Sec_ = std::clamp(t60Sec, 0.05f, 5.0f);
    }

    [[nodiscard]] float estimatedRoomT60() const noexcept {
        return estimatedRoomT60Sec_;
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

        // Coeficiente one-pole por muestra (τ = 15 ms @ 48 kHz, patrón anti-click WFS §7.5)
        const float smoothAlpha = 1.0f - std::exp(-1.0f / (kParamSmoothTauSec * kDefaultSampleRateHz));
        const float erRoomScale = RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(1.0f, estimatedRoomT60Sec_);

        for (size_t objIdx = 0; objIdx < NUM_OBJECTS; ++objIdx) {
            const float* inObj = inObjects[objIdx];
            if (!inObj) continue;

            const auto& meta = objects[objIdx];
            const float x = std::clamp(meta.position.x, -1.0f, 1.0f);
            const float z = std::clamp(meta.position.z, -1.5f, 2.5f);
            const float dHoriz = std::max(0.5f, meta.position.y);
            const float d = std::sqrt(dHoriz * dHoriz + z * z); // True 3D distance in meters

            // 1. Distance law (1 / d with safety cap) + 1-pole high frequency air damping
            //    modulated by vertical elevation (Blauert upper-hemisphere spectral cue)
            const float distGain = (1.0f / d) * std::clamp(meta.gain, 0.1f, 1.5f);
            const float elevTilt = 1.0f + 0.18f * z;
            const float hfDampAlpha = std::clamp(0.05f * d * elevTilt, 0.01f, 0.55f);

            // 2. Bilinear panning / HRTF interaural cue calculation (ITD + ILD)
            // Left & Right gain cues (constant-power sin/cos panning law)
            const float panAngle = x * kQuarterPiRad; // ~45 deg max
            const float ildTargetL = std::cos(kQuarterPiRad - panAngle) * distGain;
            const float ildTargetR = std::sin(kQuarterPiRad - panAngle) * distGain;

            // Interaural time difference: ~667us max at 48kHz -> 32 samples.
            const float itdSigned = std::sin(panAngle) * itdScale * kItdMaxSamples;
            const float itdTargetL = itdSigned > 0.0f ? std::clamp(itdSigned, 0.0f, 64.0f) : 0.0f;
            const float itdTargetR = itdSigned < 0.0f ? std::clamp(-itdSigned, 0.0f, 64.0f) : 0.0f;

            if (!tapSmoothInit_[objIdx]) {
                ildSmoothL_[objIdx] = ildTargetL;
                ildSmoothR_[objIdx] = ildTargetR;
                itdSmoothL_[objIdx] = itdTargetL;
                itdSmoothR_[objIdx] = itdTargetR;
                tapSmoothInit_[objIdx] = true;
            }

            float curIldL = ildSmoothL_[objIdx];
            float curIldR = ildSmoothR_[objIdx];
            float curItdL = itdSmoothL_[objIdx];
            float curItdR = itdSmoothR_[objIdx];

            float fltL = distFilterL_[objIdx];
            float fltR = distFilterR_[objIdx];

            auto& dBuf = delayBuffers_[objIdx];
            size_t dPos = delayBufferPos_[objIdx];

            for (size_t i = 0; i < numSamples; ++i) {
                const float s = inObj[i];

                // Suavizado one-pole por muestra (τ = 15 ms, patrón WFS §7.5)
                curIldL += smoothAlpha * (ildTargetL - curIldL);
                curIldR += smoothAlpha * (ildTargetR - curIldR);
                curItdL += smoothAlpha * (itdTargetL - curItdL);
                curItdR += smoothAlpha * (itdTargetR - curItdR);

                // Push to delay buffer FIRST — both the ITD read and the
                // early-reflections taps below draw from it, and itdL/itdR
                // == 0 must read back exactly this sample (backward compat).
                dBuf[dPos] = s;

                // Lectura fraccional ITD con interpolación lineal (cero clicks en movimiento)
                const size_t i0L = static_cast<size_t>(curItdL);
                const float fracL = curItdL - static_cast<float>(i0L);
                const float sL0 = dBuf[(dPos + 512 - i0L) & 511];
                const float sL1 = dBuf[(dPos + 512 - (i0L + 1u)) & 511];
                const float sL  = sL0 + fracL * (sL1 - sL0);

                const size_t i0R = static_cast<size_t>(curItdR);
                const float fracR = curItdR - static_cast<float>(i0R);
                const float sR0 = dBuf[(dPos + 512 - i0R) & 511];
                const float sR1 = dBuf[(dPos + 512 - (i0R + 1u)) & 511];
                const float sR  = sR0 + fracR * (sR1 - sR0);

                // Direct sound filtering (distance damping), per-ear ITD-delayed
                fltL += hfDampAlpha * (sL * curIldL - fltL);
                fltR += hfDampAlpha * (sR * curIldR - fltR);

                const float directL = fltL;
                const float directR = fltR;

                // Early reflections per object (precomputed fixed taps: 8, 17, 29, 43 samples)
                const float er1 = dBuf[(dPos + 512 - 8) & 511] * erGains_[0];
                const float er2 = dBuf[(dPos + 512 - 17) & 511] * erGains_[1];
                const float er3 = dBuf[(dPos + 512 - 29) & 511] * erGains_[2];
                const float er4 = dBuf[(dPos + 512 - 43) & 511] * erGains_[3];
                dPos = (dPos + 1) & 511;

                const float erSum = (er1 + er2 + er3 + er4) * distGain * erRoomScale;

                outL[i] += directL + erSum * kErMixScale;
                outR[i] += directR + erSum * kErMixScale;
            }

            ildSmoothL_[objIdx] = curIldL;
            ildSmoothR_[objIdx] = curIldR;
            itdSmoothL_[objIdx] = curItdL;
            itdSmoothR_[objIdx] = curItdR;
            distFilterL_[objIdx] = fltL;
            distFilterR_[objIdx] = fltR;
            delayBufferPos_[objIdx] = dPos;
        }
    }

private:
    float distFilterL_[NUM_OBJECTS]{};
    float distFilterR_[NUM_OBJECTS]{};
    float ildSmoothL_[NUM_OBJECTS]{};
    float ildSmoothR_[NUM_OBJECTS]{};
    float itdSmoothL_[NUM_OBJECTS]{};
    float itdSmoothR_[NUM_OBJECTS]{};
    bool  tapSmoothInit_[NUM_OBJECTS]{};
    float estimatedRoomT60Sec_{0.34f};
    std::array<std::array<float, 512>, NUM_OBJECTS> delayBuffers_{};
    size_t delayBufferPos_[NUM_OBJECTS]{};
    std::array<float, EARLY_REFLECTIONS_TAPS> erGains_{{0.25f, 0.18f, 0.12f, 0.08f}};
};

} // namespace ivanna::spatial
