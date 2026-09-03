#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

constexpr size_t LSTM_UNITS = 8; // Ultra-fast int8 matrix execution tuned for L1 cache fit

class Psychoacoustics {
public:
    Psychoacoustics();
    void applyMaskingCompensation(Ivanna::AudioBuffer* buffer);
    void predictAndMitigateFatigue(Ivanna::AudioBuffer* buffer);

private:
    // Quantized int8_t LSTM Weights
    ALIGN_NEON int8_t m_Wf[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wi[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wc[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wo[LSTM_UNITS][LSTM_UNITS];

    ALIGN_NEON int16_t m_h_state[LSTM_UNITS];
    ALIGN_NEON int16_t m_c_state[LSTM_UNITS];

    float m_fatigueIndex = 0.0f;
    float m_envLeft  = 0.0f;
    float m_envRight = 0.0f;

    // Gain smoothing para applyMaskingCompensation (evita clicks de ganancia)
    float m_gainSmL = 1.0f;
    float m_gainSmR = 1.0f;

    // Estado IIR persistente entre bloques (evita tronidos/pops periódicos)
    float m_iirStateL = 0.0f;
    float m_iirStateR = 0.0f;
};

} // namespace Ivanna
