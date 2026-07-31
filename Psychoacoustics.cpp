#include "Psychoacoustics.hpp"
#include <cmath>

namespace Ivanna {

Psychoacoustics::Psychoacoustics() {
    for (size_t i = 0; i < LSTM_UNITS; ++i) {
        m_h_state[i] = 0;
        m_c_state[i] = 0;
        for (size_t j = 0; j < LSTM_UNITS; ++j) {
            m_Wf[i][j] = (i == j) ? 64 : 0;
            m_Wi[i][j] = 10;
            m_Wc[i][j] = 5;
            m_Wo[i][j] = 20;
        }
    }
}

void Psychoacoustics::applyMaskingCompensation(AudioBuffer* buffer) {
    const float att = 0.001f;
    const float rel = 0.0001f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float absL = std::abs(buffer->left[i]);
        if (absL > m_envLeft) {
            m_envLeft += att * (absL - m_envLeft);
        } else {
            m_envLeft += rel * (absL - m_envLeft);
        }

        if (buffer->left[i] > 0.001f && m_envLeft > 0.1f) {
            float comp = 1.0f + (0.15f * (m_envLeft - absL));
            comp = std::min(comp, 1.3f);
            buffer->left[i] *= comp;
        }

        float absR = std::abs(buffer->right[i]);
        if (absR > m_envRight) {
            m_envRight += att * (absR - m_envRight);
        } else {
            m_envRight += rel * (absR - m_envRight);
        }

        if (buffer->right[i] > 0.001f && m_envRight > 0.1f) {
            float comp = 1.0f + (0.15f * (m_envRight - absR));
            comp = std::min(comp, 1.3f);
            buffer->right[i] *= comp;
        }
    }
}

void Psychoacoustics::predictAndMitigateFatigue(AudioBuffer* buffer) {
    float rms = 0.0f;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        rms += buffer->left[i] * buffer->left[i];
    }
    rms = std::sqrt(rms / BLOCK_SIZE);

    // leaky integrator float: reemplaza int16 que desbordaba y corrompía alpha
    constexpr float kTau = 0.9995f;
    m_fatigueIndex = kTau * m_fatigueIndex + (1.0f - kTau) * rms;
    if (m_fatigueIndex > 1.0f) m_fatigueIndex = 1.0f;

    float alpha = 1.0f - (m_fatigueIndex * 0.4f);
    float stateL = 0.0f;
    float stateR = 0.0f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        stateL = stateL + alpha * (buffer->left[i] - stateL);
        buffer->left[i] = stateL;

        stateR = stateR + alpha * (buffer->right[i] - stateR);
        buffer->right[i] = stateR;
    }
}

} // namespace Ivanna
