#include "EvolutionaryEQ.hpp"
#include <cmath>

namespace Ivanna {

EvolutionaryEQ::EvolutionaryEQ() {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_firCoeffs[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
    }
    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_historyL[i] = 0.0f;
        m_historyR[i] = 0.0f;
    }
}

void EvolutionaryEQ::calibrateTargetRoom() {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        float n = static_cast<float>(i) - static_cast<float>(FIR_TAPS) / 2.0f;
        if (n == 0.0f) {
            m_firCoeffs[i] = 1.0f;
        } else {
            float fc = 0.25f;
            m_firCoeffs[i] = (std::sin(2.0f * 3.14159265f * fc * n) / (3.14159265f * n)) *
                             (0.54f - 0.46f * std::cos(2.0f * 3.14159265f * i / (FIR_TAPS - 1)));
        }
    }
}

void EvolutionaryEQ::processNEON(AudioBuffer* buffer) {
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_historyL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_historyR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float32x4_t accL = vdupq_n_f32(0.0f);
        float32x4_t accR = vdupq_n_f32(0.0f);

        for (size_t t = 0; t < FIR_TAPS; t += 4) {
            float32x4_t coeff = vld1q_f32(&m_firCoeffs[t]);
            float32x4_t xL = vld1q_f32(&m_historyL[i + t]);
            float32x4_t xR = vld1q_f32(&m_historyR[i + t]);

            accL = vmlaq_f32(accL, coeff, xL);
            accR = vmlaq_f32(accR, coeff, xR);
        }

        buffer->left[i] = vgetq_lane_f32(accL, 0) + vgetq_lane_f32(accL, 1) +
                          vgetq_lane_f32(accL, 2) + vgetq_lane_f32(accL, 3);
        buffer->right[i] = vgetq_lane_f32(accR, 0) + vgetq_lane_f32(accR, 1) +
                           vgetq_lane_f32(accR, 2) + vgetq_lane_f32(accR, 3);
    }
#else
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float outL = 0.0f;
        float outR = 0.0f;
        for (size_t t = 0; t < FIR_TAPS; ++t) {
            outL += m_firCoeffs[t] * m_historyL[i + t];
            outR += m_firCoeffs[t] * m_historyR[i + t];
        }
        buffer->left[i] = outL;
        buffer->right[i] = outR;
    }
#endif

    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_historyL[i] = m_historyL[BLOCK_SIZE + i];
        m_historyR[i] = m_historyR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
