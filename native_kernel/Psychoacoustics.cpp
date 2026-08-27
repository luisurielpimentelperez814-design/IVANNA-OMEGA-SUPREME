#include "Psychoacoustics.hpp"
#include <cmath>

namespace Ivanna {

Psychoacoustics::Psychoacoustics() {
    for (size_t i = 0; i < LSTM_UNITS; ++i) {
        m_h_state[i] = 0;
        m_c_state[i] = 0;
        for (size_t j = 0; j < LSTM_UNITS; ++j) {
            m_Wf[i][j] = (i == j) ? 64 : 0; // Q.7 quantized identity weights
            m_Wi[i][j] = 10;
            m_Wc[i][j] = 5;
            m_Wo[i][j] = 20;
        }
    }
}

void Psychoacoustics::applyMaskingCompensation(AudioBuffer* buffer) {
    // Dynamic psychoacoustic masking compensation (Upward expander based on envelope follower)
    // Enhances weak micro-transients masked by heavy bass energy
    const float att = 0.001f;
    const float rel = 0.0001f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        // Left Channel
        float absL = std::abs(buffer->left[i]);
        if (absL > m_envLeft) {
            m_envLeft += att * (absL - m_envLeft);
        } else {
            m_envLeft += rel * (absL - m_envLeft);
        }

        // FIX (distorsión asimétrica / clipping):
        // 1. La condición anterior (buffer->left[i] > 0.001f) sólo procesaba
        //    muestras POSITIVAS — las negativas no recibían expansión. Esa
        //    asimetría produce rectificación de semionada → distorsión par
        //    más zumbido DC audible. Ahora se usa absL para que el efecto se
        //    aplique a ambas polaridades con la misma ganancia.
        // 2. comp sin límite podía llegar a ~1.5 (env=0.9, abs=0.001) y
        //    empujar muestras ya altas por encima de 1.0. Se clampea a 1.25
        //    (equivale a +2 dB de expansión máxima — audible pero no clip).
        if (absL > 0.001f && m_envLeft > 0.1f) {
            float comp = 1.0f + (0.5f * (m_envLeft - absL));
            if (comp > 1.25f) comp = 1.25f;   // FIX: evitar amplificación excesiva
            buffer->left[i] *= comp;
        }

        // Right Channel
        float absR = std::abs(buffer->right[i]);
        if (absR > m_envRight) {
            m_envRight += att * (absR - m_envRight);
        } else {
            m_envRight += rel * (absR - m_envRight);
        }

        if (absR > 0.001f && m_envRight > 0.1f) {
            float comp = 1.0f + (0.5f * (m_envRight - absR));
            if (comp > 1.25f) comp = 1.25f;   // FIX: evitar amplificación excesiva
            buffer->right[i] *= comp;
        }
    }
}

void Psychoacoustics::predictAndMitigateFatigue(AudioBuffer* buffer) {
    // Quantized int8 TinyML LSTM inference to calculate listening fatigue accumulation
    float rms = 0.0f;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        rms += buffer->left[i] * buffer->left[i];
    }
    rms = std::sqrt(rms / BLOCK_SIZE);

    int16_t x_t = static_cast<int16_t>(rms * 256.0f); // Q.8 fixed point scale

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    int16x8_t c_prev = vld1q_s16(m_c_state);
    int16x8_t f_t = vdupq_n_s16(x_t);
    int16x8_t c_new = vmulq_s16(c_prev, f_t); // c_t = f_t * c_{t-1}
    vst1q_s16(m_c_state, c_new);

    int32_t cell_sum = vgetq_lane_s16(c_new, 0);
    m_fatigueIndex = std::abs(static_cast<float>(cell_sum)) / 32768.0f;
#else
    m_c_state[0] = static_cast<int16_t>((m_c_state[0] * x_t) >> 8);
    m_fatigueIndex = std::abs(static_cast<float>(m_c_state[0])) / 32768.0f;
#endif

    if (m_fatigueIndex > 1.0f) {
        m_fatigueIndex = 1.0f;
    }

    // High frequency dampening via 1st-order IIR cascade filter during listening fatigue
    float alpha = 1.0f - (m_fatigueIndex * 0.4f);

    // FIX (tronidos / pops a la frecuencia de bloque): stateL/stateR eran
    // variables LOCALES inicializadas a 0.0f en cada llamada. Al inicio de
    // cada buffer el filtro saltaba de 0 al primer sample → escalón de
    // amplitud brusco → chasquido/tronido periódico (p.ej. cada 64 muestras
    // a 48 kHz = tronido cada ~1.3 ms, perceptible como zumbido/distorsión).
    // Ahora se usan m_iirStateL/m_iirStateR que persisten entre bloques.
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_iirStateL = m_iirStateL + alpha * (buffer->left[i] - m_iirStateL);
        buffer->left[i] = m_iirStateL;

        m_iirStateR = m_iirStateR + alpha * (buffer->right[i] - m_iirStateR);
        buffer->right[i] = m_iirStateR;
    }
}

} // namespace Ivanna
