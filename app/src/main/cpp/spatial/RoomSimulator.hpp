#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Ivanna {

struct RoomConfig {
    float roomWidthMeters = 5.0f;
    float roomLengthMeters = 7.0f;
    float roomHeightMeters = 3.0f;
    float absorptionFactor = 0.35f;
    float wetMix = 0.25f;
    float stereoSpread = 0.35f;
};

/**
 * RoomSimulator — Motor Acústico Magistral Schroeder/Moorer (Zero-Allocation RT).
 *
 * Arquitectura por canal:
 *   1. Reflexiones Tempranas (4 taps discretos primos: paredes laterales, techo, suelo).
 *   2. Banco de 2 filtros Comb paralelos con amortiguamiento pasabajos de 1 polo
 *      (absorción de aire dependiente de la frecuencia `m_dampening`).
 *   3. Difusor All-Pass de Schroeder en serie (g = 0.5) para densidad modal sin coloración.
 *   4. Decorrelación interaural cruzada (IACC control) mediante `m_stereoSpread`.
 */
class RoomSimulator {
public:
    static constexpr size_t kErBufSize   = 2048;
    static constexpr size_t kCombSizeL1  = 1423;
    static constexpr size_t kCombSizeL2  = 1601;
    static constexpr size_t kCombSizeR1  = 1499;
    static constexpr size_t kCombSizeR2  = 1693;
    static constexpr size_t kAllPassSizeL = 347;
    static constexpr size_t kAllPassSizeR = 389;

    RoomSimulator() noexcept {
        reset();
    }
    ~RoomSimulator() = default;

    void reset() noexcept {
        m_erBufL.fill(0.0f);
        m_erBufR.fill(0.0f);
        m_combBufferL1.fill(0.0f);
        m_combBufferL2.fill(0.0f);
        m_combBufferR1.fill(0.0f);
        m_combBufferR2.fill(0.0f);
        m_allPassBufL.fill(0.0f);
        m_allPassBufR.fill(0.0f);
        m_erPos = 0;
        m_combPosL1 = m_combPosL2 = 0;
        m_combPosR1 = m_combPosR2 = 0;
        m_allPassPosL = m_allPassPosR = 0;
        m_dampStateL1 = m_dampStateL2 = 0.0f;
        m_dampStateR1 = m_dampStateR2 = 0.0f;
    }

    void setConfig(const RoomConfig& config) noexcept {
        m_config = config;
        m_absorption = std::clamp(config.absorptionFactor, 0.05f, 0.95f);
        m_roomSize = std::clamp((config.roomWidthMeters + config.roomLengthMeters) * 0.05f, 0.1f, 1.0f);
        m_stereoSpread = std::clamp(config.stereoSpread, 0.0f, 1.0f);
    }

    void setParameters(float roomSize, float absorption, float dampening) noexcept {
        m_roomSize = std::clamp(roomSize, 0.1f, 1.0f);
        m_absorption = std::clamp(absorption, 0.05f, 0.95f);
        m_dampening = std::clamp(dampening, 0.05f, 0.95f);
        m_config.absorptionFactor = m_absorption;
    }

    void setWetMix(float wetMix) noexcept {
        m_config.wetMix = std::clamp(wetMix, 0.0f, 1.0f);
    }

    void setStereoSpread(float spread) noexcept {
        m_stereoSpread = std::clamp(spread, 0.0f, 1.0f);
        m_config.stereoSpread = m_stereoSpread;
    }

    float getRoomSize() const noexcept { return m_roomSize; }
    float getAbsorption() const noexcept { return m_absorption; }
    float getDampening() const noexcept { return m_dampening; }
    float getWetMix() const noexcept { return m_config.wetMix; }
    float getStereoSpread() const noexcept { return m_stereoSpread; }

    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR,
                       size_t numSamples) noexcept {
        if (!inL || !inR || !outL || !outR) return;

        const float fb = std::clamp((1.0f - m_absorption) * (0.45f + 0.47f * m_roomSize), 0.0f, 0.92f);
        const float damp = std::clamp(m_dampening, 0.05f, 0.90f);
        const float dampInv = 1.0f - damp;
        const float wet = std::clamp(m_config.wetMix, 0.0f, 1.0f);
        const float dry = 1.0f - 0.45f * wet;
        const float xfeed = 0.22f * m_stereoSpread;

        // Taps de reflexiones tempranas escalados por el tamaño de sala
        const size_t erTap0 = static_cast<size_t>(343.0f * (0.5f + 0.5f * m_roomSize));
        const size_t erTap1 = static_cast<size_t>(613.0f * (0.5f + 0.5f * m_roomSize));
        const size_t erTap2 = static_cast<size_t>(883.0f * (0.5f + 0.5f * m_roomSize));
        const size_t erTap3 = static_cast<size_t>(1181.0f * (0.5f + 0.5f * m_roomSize));

        for (size_t i = 0; i < numSamples; ++i) {
            const float xL = inL[i];
            const float xR = inR[i];

            // 1. Escribir en línea de retardo de reflexiones tempranas
            m_erBufL[m_erPos] = xL;
            m_erBufR[m_erPos] = xR;

            const size_t idx0 = (m_erPos + kErBufSize - erTap0) & (kErBufSize - 1);
            const size_t idx1 = (m_erPos + kErBufSize - erTap1) & (kErBufSize - 1);
            const size_t idx2 = (m_erPos + kErBufSize - erTap2) & (kErBufSize - 1);
            const size_t idx3 = (m_erPos + kErBufSize - erTap3) & (kErBufSize - 1);
            m_erPos = (m_erPos + 1) & (kErBufSize - 1);

            const float erL = 0.36f * m_erBufL[idx0] + 0.26f * m_erBufR[idx1] +
                              0.18f * m_erBufL[idx2] + 0.12f * m_erBufR[idx3];
            const float erR = 0.36f * m_erBufR[idx0] + 0.26f * m_erBufL[idx1] +
                              0.18f * m_erBufR[idx2] + 0.12f * m_erBufL[idx3];

            // 2. Filtros Comb con amortiguamiento HF de 1 polo en el lazo de realimentación
            const float cL1 = m_combBufferL1[m_combPosL1];
            const float cL2 = m_combBufferL2[m_combPosL2];
            const float cR1 = m_combBufferR1[m_combPosR1];
            const float cR2 = m_combBufferR2[m_combPosR2];

            m_dampStateL1 = dampInv * cL1 + damp * m_dampStateL1;
            m_dampStateL2 = dampInv * cL2 + damp * m_dampStateL2;
            m_dampStateR1 = dampInv * cR1 + damp * m_dampStateR1;
            m_dampStateR2 = dampInv * cR2 + damp * m_dampStateR2;

            const float feedL = xL + 0.25f * erL;
            const float feedR = xR + 0.25f * erR;

            m_combBufferL1[m_combPosL1] = feedL + m_dampStateL1 * fb;
            m_combBufferL2[m_combPosL2] = feedL + m_dampStateL2 * (fb * 0.91f);
            m_combBufferR1[m_combPosR1] = feedR + m_dampStateR1 * fb;
            m_combBufferR2[m_combPosR2] = feedR + m_dampStateR2 * (fb * 0.91f);

            if (++m_combPosL1 >= kCombSizeL1) m_combPosL1 = 0;
            if (++m_combPosL2 >= kCombSizeL2) m_combPosL2 = 0;
            if (++m_combPosR1 >= kCombSizeR1) m_combPosR1 = 0;
            if (++m_combPosR2 >= kCombSizeR2) m_combPosR2 = 0;

            const float tailL = 0.5f * (cL1 + cL2);
            const float tailR = 0.5f * (cR1 + cR2);

            // 3. Difusor All-Pass de Schroeder (g = 0.5)
            constexpr float kApGain = 0.5f;
            const float bufApL = m_allPassBufL[m_allPassPosL];
            const float apInL  = tailL + erL * 0.45f;
            const float apOutL = -kApGain * apInL + bufApL;
            m_allPassBufL[m_allPassPosL] = apInL + kApGain * bufApL;
            if (++m_allPassPosL >= kAllPassSizeL) m_allPassPosL = 0;

            const float bufApR = m_allPassBufR[m_allPassPosR];
            const float apInR  = tailR + erR * 0.45f;
            const float apOutR = -kApGain * apInR + bufApR;
            m_allPassBufR[m_allPassPosR] = apInR + kApGain * bufApR;
            if (++m_allPassPosR >= kAllPassSizeR) m_allPassPosR = 0;

            // 4. Decorrelación interaural cruzada (IACC) y mezcla final
            const float revL = apOutL + xfeed * apOutR;
            const float revR = apOutR + xfeed * apOutL;

            outL[i] = dry * xL + wet * revL;
            outR[i] = dry * xR + wet * revR;
        }
    }

    void processReverb(const float* inL, const float* inR,
                       float* outL, float* outR,
                       size_t numFrames) noexcept {
        processStereo(inL, inR, outL, outR, numFrames);
    }

private:
    RoomConfig m_config{};
    float m_roomSize{0.5f};
    float m_absorption{0.35f};
    float m_dampening{0.4f};
    float m_stereoSpread{0.35f};

    std::array<float, kErBufSize> m_erBufL{};
    std::array<float, kErBufSize> m_erBufR{};
    std::array<float, kCombSizeL1> m_combBufferL1{};
    std::array<float, kCombSizeL2> m_combBufferL2{};
    std::array<float, kCombSizeR1> m_combBufferR1{};
    std::array<float, kCombSizeR2> m_combBufferR2{};
    std::array<float, kAllPassSizeL> m_allPassBufL{};
    std::array<float, kAllPassSizeR> m_allPassBufR{};

    size_t m_erPos{0};
    size_t m_combPosL1{0}, m_combPosL2{0};
    size_t m_combPosR1{0}, m_combPosR2{0};
    size_t m_allPassPosL{0}, m_allPassPosR{0};
    float m_dampStateL1{0.0f}, m_dampStateL2{0.0f};
    float m_dampStateR1{0.0f}, m_dampStateR2{0.0f};
};

} // namespace Ivanna
