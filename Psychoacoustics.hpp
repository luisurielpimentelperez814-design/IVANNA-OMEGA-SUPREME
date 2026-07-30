#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

constexpr size_t LSTM_UNITS = 8;

class Psychoacoustics {
public:
    Psychoacoustics();
    void applyMaskingCompensation(AudioBuffer* buffer);
    void predictAndMitigateFatigue(AudioBuffer* buffer);

private:
    ALIGN_NEON int8_t m_Wf[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wi[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wc[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wo[LSTM_UNITS][LSTM_UNITS];

    ALIGN_NEON int16_t m_h_state[LSTM_UNITS];
    ALIGN_NEON int16_t m_c_state[LSTM_UNITS];

    float m_fatigueIndex = 0.0f;
    float m_envLeft = 0.0f;
    float m_envRight = 0.0f;
};

} // namespace Ivanna
