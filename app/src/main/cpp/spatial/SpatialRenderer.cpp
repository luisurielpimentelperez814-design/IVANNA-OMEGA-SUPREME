#include "SpatialRenderer.hpp"
#include <cstring>

namespace Ivanna {

BinauralRenderer::BinauralRenderer() {
    std::memset(m_hrtfLeft, 0, sizeof(m_hrtfLeft));
    std::memset(m_hrtfRight, 0, sizeof(m_hrtfRight));
    std::memset(m_history, 0, sizeof(m_history));

    // Initialize minimum-phase HRTF synthetic impulse response pair
    for (size_t i = 0; i < HRTF_TAPS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(HRTF_TAPS);
        m_hrtfLeft[i] = std::exp(-15.0f * t) * std::sin(2.0f * M_PI * 4.0f * t);
        m_hrtfRight[i] = std::exp(-18.0f * t) * std::sin(2.0f * M_PI * 4.0f * t + 0.2f);
    }
}

void BinauralRenderer::setOrientation(const Quaternion& quat) {
    m_orientation = quat;
}

void BinauralRenderer::processBinaural(
    const float* inMono,
    float* outLeft,
    float* outRight,
    size_t numFrames,
    Vector3D position
) {
    // Apply head-tracking rotation correction to object azimuth
    float yaw = std::atan2(2.0f * (m_orientation.w * m_orientation.z + m_orientation.x * m_orientation.y),
                           1.0f - 2.0f * (m_orientation.y * m_orientation.y + m_orientation.z * m_orientation.z));
    
    float rotX = position.x * std::cos(yaw) - position.y * std::sin(yaw);
    float rotY = position.x * std::sin(yaw) + position.y * std::cos(yaw);

    float pannedLeftGain = std::clamp(0.5f * (1.0f - rotX), 0.0f, 1.0f);
    float pannedRightGain = std::clamp(0.5f * (1.0f + rotX), 0.0f, 1.0f);

    for (size_t f = 0; f < numFrames; ++f) {
        m_history[m_histIdx] = inMono[f];

        float sumL = 0.0f;
        float sumR = 0.0f;

        for (size_t t = 0; t < HRTF_TAPS; ++t) {
            size_t tapIdx = (m_histIdx + HRTF_TAPS - t) % HRTF_TAPS;
            sumL += m_history[tapIdx] * m_hrtfLeft[t];
            sumR += m_history[tapIdx] * m_hrtfRight[t];
        }

        outLeft[f] = sumL * pannedLeftGain;
        outRight[f] = sumR * pannedRightGain;

        m_histIdx = (m_histIdx + 1) % HRTF_TAPS;
    }
}

RoomSimulator::RoomSimulator() {
    m_delayBufferL.resize(4800, 0.0f); // ~100ms at 48kHz
    m_delayBufferR.resize(5200, 0.0f);
}

void RoomSimulator::setParameters(float roomSize, float absorption, float dampening) {
    m_roomSize = std::clamp(roomSize, 0.0f, 1.0f);
    m_absorption = std::clamp(absorption, 0.0f, 1.0f);
    m_dampening = std::clamp(dampening, 0.0f, 1.0f);
}

void RoomSimulator::processReverb(
    const float* inL,
    const float* inR,
    float* outL,
    float* outR,
    size_t numFrames
) {
    float feedback = 0.2f + m_roomSize * 0.65f;
    size_t delayLenL = static_cast<size_t>(1000 + m_roomSize * 3500);
    size_t delayLenR = static_cast<size_t>(1100 + m_roomSize * 3800);

    for (size_t i = 0; i < numFrames; ++i) {
        float readL = m_delayBufferL[m_delayIdxL % delayLenL];
        float readR = m_delayBufferR[m_delayIdxR % delayLenR];

        m_delayBufferL[m_delayIdxL % delayLenL] = inL[i] + readR * feedback * (1.0f - m_absorption);
        m_delayBufferR[m_delayIdxR % delayLenR] = inR[i] + readL * feedback * (1.0f - m_absorption);

        outL[i] = inL[i] + readL * 0.25f;
        outR[i] = inR[i] + readR * 0.25f;

        m_delayIdxL++;
        m_delayIdxR++;
    }
}

} // namespace Ivanna
