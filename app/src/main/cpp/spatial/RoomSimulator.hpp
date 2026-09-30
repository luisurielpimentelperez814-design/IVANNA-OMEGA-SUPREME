#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Ivanna {

struct RoomConfig {
    float roomWidthMeters = 5.0f;
    float roomLengthMeters = 7.0f;
    float roomHeightMeters = 3.0f;
    float absorptionFactor = 0.35f;
    float wetMix = 0.25f;
};

class RoomSimulator {
public:
    RoomSimulator()
        : m_combBufferL1(1423, 0.0f),
          m_combBufferL2(1601, 0.0f),
          m_combBufferR1(1499, 0.0f),
          m_combBufferR2(1693, 0.0f) {}
    ~RoomSimulator() = default;

    void setConfig(const RoomConfig& config) noexcept {
        m_config = config;
        m_absorption = std::clamp(config.absorptionFactor, 0.05f, 0.95f);
        m_roomSize = std::clamp((config.roomWidthMeters + config.roomLengthMeters) * 0.05f, 0.1f, 1.0f);
    }

    void setParameters(float roomSize, float absorption, float dampening) noexcept {
        m_roomSize = std::clamp(roomSize, 0.1f, 1.0f);
        m_absorption = std::clamp(absorption, 0.05f, 0.95f);
        m_dampening = std::clamp(dampening, 0.05f, 0.95f);
        m_config.absorptionFactor = m_absorption;
    }

    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR,
                       size_t numSamples) noexcept {
        if (!inL || !inR || !outL || !outR) return;
        const float fb = std::clamp((1.0f - m_config.absorptionFactor) * m_roomSize, 0.0f, 0.92f);
        const float wet = std::clamp(m_config.wetMix, 0.0f, 1.0f);
        const float dry = 1.0f - wet;

        for (size_t i = 0; i < numSamples; ++i) {
            const float cL1 = m_combBufferL1[m_combPosL1];
            const float cL2 = m_combBufferL2[m_combPosL2];
            const float cR1 = m_combBufferR1[m_combPosR1];
            const float cR2 = m_combBufferR2[m_combPosR2];

            m_combBufferL1[m_combPosL1] = inL[i] + cL1 * fb * (1.0f - 0.5f * m_dampening);
            m_combBufferL2[m_combPosL2] = inL[i] + cL2 * (fb * 0.85f);
            m_combBufferR1[m_combPosR1] = inR[i] + cR1 * fb * (1.0f - 0.5f * m_dampening);
            m_combBufferR2[m_combPosR2] = inR[i] + cR2 * (fb * 0.85f);

            if (++m_combPosL1 >= m_combBufferL1.size()) m_combPosL1 = 0;
            if (++m_combPosL2 >= m_combBufferL2.size()) m_combPosL2 = 0;
            if (++m_combPosR1 >= m_combBufferR1.size()) m_combPosR1 = 0;
            if (++m_combPosR2 >= m_combBufferR2.size()) m_combPosR2 = 0;

            outL[i] = dry * inL[i] + wet * 0.5f * (cL1 + cL2);
            outR[i] = dry * inR[i] + wet * 0.5f * (cR1 + cR2);
        }
    }

    void processReverb(const float* inL, const float* inR,
                       float* outL, float* outR,
                       size_t numFrames) noexcept {
        processStereo(inL, inR, outL, outR, numFrames);
    }

private:
    RoomConfig m_config;
    float m_roomSize{0.5f};
    float m_absorption{0.35f};
    float m_dampening{0.4f};
    std::vector<float> m_combBufferL1, m_combBufferL2;
    std::vector<float> m_combBufferR1, m_combBufferR2;
    size_t m_combPosL1{0}, m_combPosL2{0};
    size_t m_combPosR1{0}, m_combPosR2{0};
};

} // namespace Ivanna
