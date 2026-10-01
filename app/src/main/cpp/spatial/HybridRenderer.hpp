#pragma once

#include "HRTFInterpolator.hpp"
#include "RoomSimulator.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Ivanna {

struct NativeAudioObject {
    int id;
    float posX, posY, posZ;
    float gain;
};

/**
 * HybridRenderer — Motor Espacial Híbrido Magistral (HRTF KEMAR 128-Tap + Sala Schroeder/Moorer).
 *
 * Sintetiza una escena binaural tridimensional combinando:
 *   1. Interpolación HRTF bilineal esférica (`m_hrtfInterpolator`) de 128 taps para
 *      el par estéreo virtual (±azimut, elevación) y los objetos 3D activos (`m_activeObjects`).
 *   2. Convolución FIR sobre líneas de retardo continuas (`m_delayHistoryL`, `m_delayHistoryR`)
 *      sin asignaciones de heap en el hilo de audio.
 *   3. Simulación acústica de sala Schroeder/Moorer (`m_roomSimulator`) con reflexiones
 *      tempranas, absorción de aire HF y difusión all-pass.
 */
class HybridRenderer {
public:
    static constexpr size_t kActiveTaps = 128;
    static constexpr size_t kMaxObjects = 16;

    HybridRenderer() noexcept {
        reset();
        refreshHrtfPair();
    }
    ~HybridRenderer() = default;

    void reset() noexcept {
        std::memset(m_delayHistoryL, 0, sizeof(m_delayHistoryL));
        std::memset(m_delayHistoryR, 0, sizeof(m_delayHistoryR));
        m_histPos = 0;
        m_roomSimulator.reset();
    }

    void updateObjects(const NativeAudioObject* objects, size_t count) noexcept {
        if (!objects || count == 0) {
            m_activeObjectCount.store(0, std::memory_order_release);
            m_filtersDirty.store(true, std::memory_order_release);
            return;
        }
        const size_t n = std::min(count, kMaxObjects);
        for (size_t i = 0; i < n; ++i) {
            m_activeObjects[i] = objects[i];
        }
        m_activeObjectCount.store(n, std::memory_order_release);
        m_filtersDirty.store(true, std::memory_order_release);
    }

    void setRoomConfig(const RoomConfig& config) noexcept {
        m_roomSimulator.setConfig(config);
    }

    void setEnabled(bool enabled) noexcept {
        m_enabled.store(enabled, std::memory_order_release);
    }
    bool isEnabled() const noexcept {
        return m_enabled.load(std::memory_order_acquire);
    }

    void setBinauralWet(float wet) noexcept {
        m_binauralWet.store(std::clamp(wet, 0.0f, 1.0f), std::memory_order_release);
    }
    float getBinauralWet() const noexcept {
        return m_binauralWet.load(std::memory_order_acquire);
    }

    void setVirtualAngles(float azimuthDeg, float elevationDeg) noexcept {
        const float az = std::clamp(azimuthDeg, -180.0f, 180.0f);
        const float el = std::clamp(elevationDeg, -45.0f, 45.0f);
        m_virtualAzimuthDeg.store(az, std::memory_order_release);
        m_virtualElevationDeg.store(el, std::memory_order_release);
        m_filtersDirty.store(true, std::memory_order_release);
    }

    void setRoomParameters(float roomSize, float absorption, float dampening, float wetMix, float stereoSpread = 0.35f) noexcept {
        m_roomSimulator.setParameters(roomSize, absorption, dampening);
        m_roomSimulator.setWetMix(wetMix);
        m_roomSimulator.setStereoSpread(stereoSpread);
    }

    void getTelemetry(float out[8]) const noexcept {
        if (!out) return;
        out[0] = m_enabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        out[1] = m_binauralWet.load(std::memory_order_relaxed);
        out[2] = m_virtualAzimuthDeg.load(std::memory_order_relaxed);
        out[3] = m_virtualElevationDeg.load(std::memory_order_relaxed);
        out[4] = m_roomSimulator.getRoomSize();
        out[5] = m_roomSimulator.getAbsorption();
        out[6] = m_roomSimulator.getDampening();
        out[7] = m_roomSimulator.getWetMix();
    }

    const HRTFInterpolator& hrtfInterpolator() const noexcept {
        return m_hrtfInterpolator;
    }

    void renderBinaural(const float* inStereo, float* outStereo, size_t frameCount) noexcept {
        if (!inStereo || !outStereo || frameCount == 0) return;
        const size_t n = std::min(frameCount, MAX_FRAMES);

        if (m_filtersDirty.exchange(false, std::memory_order_acq_rel)) {
            refreshHrtfPair();
        }

        const float binWet = m_binauralWet.load(std::memory_order_relaxed);
        const float binDry = 1.0f - 0.55f * binWet;

        float tmpL[MAX_FRAMES]{};
        float tmpR[MAX_FRAMES]{};

        // Convolución FIR binaural completa de 128 taps sobre doble buffer espejo contiguo
        // (sin saltos condicionales en el lazo interno -> vectorización SIMD/NEON directa)
        for (size_t i = 0; i < n; ++i) {
            const float xL = inStereo[2 * i];
            const float xR = inStereo[2 * i + 1];

            m_histPos = (m_histPos == 0) ? (HRTF_TAPS - 1) : (m_histPos - 1);
            m_delayHistoryL[m_histPos] = xL;
            m_delayHistoryL[m_histPos + HRTF_TAPS] = xL;
            m_delayHistoryR[m_histPos] = xR;
            m_delayHistoryR[m_histPos + HRTF_TAPS] = xR;

            const float* __restrict histL = &m_delayHistoryL[m_histPos];
            const float* __restrict histR = &m_delayHistoryR[m_histPos];
            float convL = 0.0f;
            float convR = 0.0f;
            for (size_t t = 0; t < kActiveTaps; ++t) {
                const float sL = histL[t];
                const float sR = histR[t];
                convL += sL * m_hrtfLL[t] + sR * m_hrtfRL[t];
                convR += sL * m_hrtfLR[t] + sR * m_hrtfRR[t];
            }

            tmpL[i] = binDry * xL + binWet * convL;
            tmpR[i] = binDry * xR + binWet * convR;
        }

        // Proyección acústica de sala Schroeder/Moorer (Early Reflections + Damped Comb + All-Pass)
        m_roomSimulator.processStereo(tmpL, tmpR, tmpL, tmpR, n);

        for (size_t i = 0; i < n; ++i) {
            outStereo[2 * i]     = tmpL[i];
            outStereo[2 * i + 1] = tmpR[i];
        }
    }

    void renderPlanar(float* inOutL, float* inOutR, size_t frameCount) noexcept {
        if (!inOutL || !inOutR || frameCount == 0) return;
        const bool wantOn = m_enabled.load(std::memory_order_relaxed);
        if (!wantOn && m_enableSmooth <= 1.0e-5f) {
            return;
        }

        if (m_filtersDirty.exchange(false, std::memory_order_acq_rel)) {
            refreshHrtfPair();
        }

        const float targetEnable = wantOn ? 1.0f : 0.0f;
        const float binWet = m_binauralWet.load(std::memory_order_relaxed);
        const float binDry = 1.0f - 0.55f * binWet;

        size_t offset = 0;
        while (offset < frameCount) {
            const size_t n = std::min(frameCount - offset, MAX_FRAMES);
            float* chL = inOutL + offset;
            float* chR = inOutR + offset;

            float tmpL[MAX_FRAMES]{};
            float tmpR[MAX_FRAMES]{};
            float dryBufL[MAX_FRAMES]{};
            float dryBufR[MAX_FRAMES]{};

            for (size_t i = 0; i < n; ++i) {
                const float xL = chL[i];
                const float xR = chR[i];
                dryBufL[i] = xL;
                dryBufR[i] = xR;

                m_histPos = (m_histPos == 0) ? (HRTF_TAPS - 1) : (m_histPos - 1);
                m_delayHistoryL[m_histPos] = xL;
                m_delayHistoryL[m_histPos + HRTF_TAPS] = xL;
                m_delayHistoryR[m_histPos] = xR;
                m_delayHistoryR[m_histPos + HRTF_TAPS] = xR;

                const float* __restrict histL = &m_delayHistoryL[m_histPos];
                const float* __restrict histR = &m_delayHistoryR[m_histPos];
                float convL = 0.0f;
                float convR = 0.0f;
                for (size_t t = 0; t < kActiveTaps; ++t) {
                    const float sL = histL[t];
                    const float sR = histR[t];
                    convL += sL * m_hrtfLL[t] + sR * m_hrtfRL[t];
                    convR += sL * m_hrtfLR[t] + sR * m_hrtfRR[t];
                }

                tmpL[i] = binDry * xL + binWet * convL;
                tmpR[i] = binDry * xR + binWet * convR;
            }

            m_roomSimulator.processStereo(tmpL, tmpR, tmpL, tmpR, n);

            for (size_t i = 0; i < n; ++i) {
                m_enableSmooth += 0.004f * (targetEnable - m_enableSmooth);
                const float g = std::clamp(m_enableSmooth, 0.0f, 1.0f);
                chL[i] = dryBufL[i] * (1.0f - g) + tmpL[i] * g;
                chR[i] = dryBufR[i] * (1.0f - g) + tmpR[i] * g;
            }

            offset += n;
        }
    }

private:
    void refreshHrtfPair() noexcept {
        float azSpread = m_virtualAzimuthDeg.load(std::memory_order_relaxed);
        float elDeg    = m_virtualElevationDeg.load(std::memory_order_relaxed);

        // Si existen objetos 3D activos, ponderar el centroide angular con el objeto dominante
        const size_t activeCount = m_activeObjectCount.load(std::memory_order_acquire);
        if (activeCount > 0) {
            const auto& obj = m_activeObjects[0];
            constexpr float kRadToDeg = 57.2957795f;
            const float objAz = std::atan2(obj.posX, std::max(1.0e-4f, obj.posY)) * kRadToDeg;
            const float objEl = std::atan2(obj.posZ, std::max(1.0e-4f, std::sqrt(obj.posX * obj.posX + obj.posY * obj.posY))) * kRadToDeg;
            azSpread = std::clamp(azSpread + 0.5f * objAz, -180.0f, 180.0f);
            elDeg    = std::clamp(elDeg + 0.5f * objEl, -45.0f, 45.0f);
        }

        const float leftSpeakerAz  = std::fmod(360.0f - std::max(15.0f, std::fabs(azSpread)) + azSpread, 360.0f);
        const float rightSpeakerAz = std::fmod(std::max(15.0f, std::fabs(azSpread)) + azSpread + 360.0f, 360.0f);

        m_hrtfInterpolator.getInterpolatedHRTF(leftSpeakerAz,  elDeg, m_hrtfLL, m_hrtfLR);
        m_hrtfInterpolator.getInterpolatedHRTF(rightSpeakerAz, elDeg, m_hrtfRL, m_hrtfRR);

        // Normalización de energía L2 sobre los 128 taps causales activos
        float energyL = 0.0f, energyR = 0.0f;
        for (size_t t = 0; t < kActiveTaps; ++t) {
            energyL += m_hrtfLL[t] * m_hrtfLL[t] + m_hrtfRL[t] * m_hrtfRL[t];
            energyR += m_hrtfLR[t] * m_hrtfLR[t] + m_hrtfRR[t] * m_hrtfRR[t];
        }
        if (energyL > 1.0e-6f && energyR > 1.0e-6f) {
            const float normL = 0.85f / std::sqrt(energyL);
            const float normR = 0.85f / std::sqrt(energyR);
            for (size_t t = 0; t < kActiveTaps; ++t) {
                m_hrtfLL[t] *= normL;
                m_hrtfRL[t] *= normL;
                m_hrtfLR[t] *= normR;
                m_hrtfRR[t] *= normR;
            }
        } else {
            std::memset(m_hrtfLL, 0, sizeof(m_hrtfLL));
            std::memset(m_hrtfLR, 0, sizeof(m_hrtfLR));
            std::memset(m_hrtfRL, 0, sizeof(m_hrtfRL));
            std::memset(m_hrtfRR, 0, sizeof(m_hrtfRR));
            m_hrtfLL[0] = 0.85f;
            m_hrtfRR[0] = 0.85f;
            m_hrtfLR[2] = 0.18f;
            m_hrtfRL[2] = 0.18f;
        }
    }

    HRTFInterpolator m_hrtfInterpolator{};
    RoomSimulator m_roomSimulator{};
    std::array<NativeAudioObject, kMaxObjects> m_activeObjects{};
    std::atomic<size_t> m_activeObjectCount{0};

    std::atomic<bool>  m_enabled{true};
    std::atomic<bool>  m_filtersDirty{true};
    std::atomic<float> m_binauralWet{0.65f};
    std::atomic<float> m_virtualAzimuthDeg{30.0f};
    std::atomic<float> m_virtualElevationDeg{0.0f};
    float              m_enableSmooth{0.0f};

    static constexpr size_t MAX_FRAMES = 1024;
    alignas(64) float m_delayHistoryL[HRTF_TAPS * 2]{};
    alignas(64) float m_delayHistoryR[HRTF_TAPS * 2]{};
    size_t m_histPos{0};

    alignas(64) float m_hrtfLL[HRTF_TAPS]{};
    alignas(64) float m_hrtfLR[HRTF_TAPS]{};
    alignas(64) float m_hrtfRL[HRTF_TAPS]{};
    alignas(64) float m_hrtfRR[HRTF_TAPS]{};
};

} // namespace Ivanna
