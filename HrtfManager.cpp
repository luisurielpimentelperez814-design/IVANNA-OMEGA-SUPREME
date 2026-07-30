#include "HrtfManager.hpp"
#include <cmath>

namespace Ivanna {

HrtfManager::HrtfManager() {
    for (size_t i = 0; i < HRTF_TAPS; ++i) {
        float t = static_cast<float>(i) / HRTF_TAPS;
        m_hrtfLL[i] = std::exp(-t * 10.0f) * std::cos(t * 30.0f);
        m_hrtfRR[i] = m_hrtfLL[i];

        float t_cross = t - 0.1f;
        m_hrtfLR[i] = (t_cross > 0.0f) ? (0.3f * std::exp(-t_cross * 15.0f)) : 0.0f;
        m_hrtfRL[i] = m_hrtfLR[i];
    }

    for (size_t i = 0; i < BLOCK_SIZE + HRTF_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }
}

void HrtfManager::processBinauralScene(AudioBuffer* buffer) {
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[HRTF_TAPS - 1 + i] = buffer->left[i];
        m_histR[HRTF_TAPS - 1 + i] = buffer->right[i];
    }

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float32x4_t outLL = vdupq_n_f32(0.0f);
        float32x4_t outLR = vdupq_n_f32(0.0f);
        float32x4_t outRR = vdupq_n_f32(0.0f);
        float32x4_t outRL = vdupq_n_f32(0.0f);

        for (size_t t = 0; t < HRTF_TAPS; t += 4) {
            float32x4_t xL = vld1q_f32(&m_histL[i + t]);
            float32x4_t xR = vld1q_f32(&m_histR[i + t]);

            float32x4_t hLL = vld1q_f32(&m_hrtfLL[t]);
            float32x4_t hLR = vld1q_f32(&m_hrtfLR[t]);
            float32x4_t hRR = vld1q_f32(&m_hrtfRR[t]);
            float32x4_t hRL = vld1q_f32(&m_hrtfRL[t]);

            outLL = vmlaq_f32(outLL, hLL, xL);
            outLR = vmlaq_f32(outLR, hLR, xL);
            outRR = vmlaq_f32(outRR, hRR, xR);
            outRL = vmlaq_f32(outRL, hRL, xR);
        }

        auto sum_vec = [](float32x4_t v) {
            return vgetq_lane_f32(v, 0) + vgetq_lane_f32(v, 1) + vgetq_lane_f32(v, 2) + vgetq_lane_f32(v, 3);
        };

        buffer->left[i] = sum_vec(outLL) + sum_vec(outRL);
        buffer->right[i] = sum_vec(outRR) + sum_vec(outLR);
    }
#else
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float outL = 0.0f;
        float outR = 0.0f;
        for (size_t t = 0; t < HRTF_TAPS; ++t) {
            float xL = m_histL[i + t];
            float xR = m_histR[i + t];

            outL += xL * m_hrtfLL[t] + xR * m_hrtfRL[t];
            outR += xR * m_hrtfRR[t] + xL * m_hrtfLR[t];
        }
        buffer->left[i] = outL;
        buffer->right[i] = outR;
    }
#endif

    for (size_t i = 0; i < HRTF_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
