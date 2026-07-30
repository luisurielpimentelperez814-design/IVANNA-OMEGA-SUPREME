#include "HrtfManager.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

HrtfManager::HrtfManager() {
    std::fill(std::begin(m_histL), std::end(m_histL), 0.0f);
    std::fill(std::begin(m_histR), std::end(m_histR), 0.0f);
    std::fill(std::begin(m_hrtfLL), std::end(m_hrtfLL), 0.0f);
    std::fill(std::begin(m_hrtfRL), std::end(m_hrtfRL), 0.0f);
    std::fill(std::begin(m_hrtfRR), std::end(m_hrtfRR), 0.0f);
    std::fill(std::begin(m_hrtfLR), std::end(m_hrtfLR), 0.0f);

    m_hrtfLL[0] = 1.0f;
    m_hrtfRR[0] = 1.0f;
}

void HrtfManager::processSpatialHrtf(AudioBuffer* buffer, float azimuth, float elevation) {
    (void)elevation;
    float rad = azimuth * (3.14159265f / 180.0f);
    float panL = std::cos(0.5f * (rad + 1.57079632f));
    float panR = std::sin(0.5f * (rad + 1.57079632f));

    m_hrtfLL[0] = panL;
    m_hrtfRR[0] = panR;
    m_hrtfRL[0] = panR * 0.15f;
    m_hrtfLR[0] = panL * 0.15f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float outL = 0.0f;
        float outR = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            float xL = m_histL[i + t];
            float xR = m_histR[i + t];

            outL += xL * m_hrtfLL[t] + xR * m_hrtfRL[t];
            outR += xR * m_hrtfRR[t] + xL * m_hrtfLR[t];
        }

        buffer->left[i] = outL;
        buffer->right[i] = outR;
    }

    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
