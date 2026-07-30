#include "RoomSimulator.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

RoomSimulator::RoomSimulator() {
    m_combBufferL1.resize(1116, 0.0f);
    m_combBufferL2.resize(1356, 0.0f);
    m_combBufferR1.resize(1188, 0.0f);
    m_combBufferR2.resize(1416, 0.0f);
}

void RoomSimulator::setConfig(const RoomConfig& config) {
    m_config = config;
}

void RoomSimulator::processStereo(const float* inL, const float* inR,
                                   float* outL, float* outR,
                                   size_t numSamples) {
    const float feedback = std::clamp(1.0f - m_config.absorptionFactor, 0.1f, 0.85f);
    const float wet = m_config.wetMix;

    for (size_t i = 0; i < numSamples; ++i) {
        float xL = inL[i];
        float xR = inR[i];

        float yL1 = m_combBufferL1[m_combPosL1];
        m_combBufferL1[m_combPosL1] = xL + yL1 * feedback;
        m_combPosL1 = (m_combPosL1 + 1) % m_combBufferL1.size();

        float yL2 = m_combBufferL2[m_combPosL2];
        m_combBufferL2[m_combPosL2] = xL + yL2 * feedback;
        m_combPosL2 = (m_combPosL2 + 1) % m_combBufferL2.size();

        float yR1 = m_combBufferR1[m_combPosR1];
        m_combBufferR1[m_combPosR1] = xR + yR1 * feedback;
        m_combPosR1 = (m_combPosR1 + 1) % m_combBufferR1.size();

        float yR2 = m_combBufferR2[m_combPosR2];
        m_combBufferR2[m_combPosR2] = xR + yR2 * feedback;
        m_combPosR2 = (m_combPosR2 + 1) % m_combBufferR2.size();

        float revL = (yL1 + yL2) * 0.5f;
        float revR = (yR1 + yR2) * 0.5f;

        outL[i] = xL * (1.0f - wet) + revL * wet;
        outR[i] = xR * (1.0f - wet) + revR * wet;
    }
}

} // namespace Ivanna
