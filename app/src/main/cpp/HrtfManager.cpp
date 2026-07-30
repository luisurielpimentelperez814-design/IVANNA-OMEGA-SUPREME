#include "HrtfManager.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

HrtfManager::HrtfManager() {
    m_hrtfLL.fill(0.0f);
    m_hrtfLR.fill(0.0f);
    m_hrtfRL.fill(0.0f);
    m_hrtfRR.fill(0.0f);

    m_hrtfLL[0] = 1.0f;
    m_hrtfRR[0] = 1.0f;

    m_histL.fill(0.0f);
    m_histR.fill(0.0f);
}

void HrtfManager::setAzimuthElevation(float azimuthDeg, float elevationDeg) noexcept {
    float rad = azimuthDeg * (3.14159265358979323846f / 180.0f);
    float pan = 0.5f * (std::sin(rad) + 1.0f);

    m_hrtfLL[0] = std::cos(pan * 1.57079632679f);
    m_hrtfRR[0] = std::sin(pan * 1.57079632679f);
    m_hrtfLR[0] = 0.1f * m_hrtfRR[0];
    m_hrtfRL[0] = 0.1f * m_hrtfLL[0];
}

void HrtfManager::processSpatial(const float* inL, const float* inR, float* outL, float* outR, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        m_histL[BLOCK_SIZE + i] = inL[i];
        m_histR[BLOCK_SIZE + i] = inR[i];
    }

    for (size_t i = 0; i < numSamples; ++i) {
        float accL = 0.0f;
        float accR = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            float xL = m_histL[i + t];
            float xR = m_histR[i + t];

            accL += xL * m_hrtfLL[t] + xR * m_hrtfRL[t];
            accR += xR * m_hrtfRR[t] + xL * m_hrtfLR[t];
        }

        outL[i] = accL;
        outR[i] = accR;
    }

    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
