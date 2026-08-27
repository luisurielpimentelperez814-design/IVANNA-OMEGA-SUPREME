#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

constexpr size_t LSTM_UNITS = 8; // Ultra-fast int8 matrix execution tuned for L1 cache fit

class Psychoacoustics {
public:
    Psychoacoustics();
    void applyMaskingCompensation(AudioBuffer* buffer);
    void predictAndMitigateFatigue(AudioBuffer* buffer);

private:
    // Quantized int8_t LSTM Weights
    ALIGN_NEON int8_t m_Wf[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wi[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wc[LSTM_UNITS][LSTM_UNITS];
    ALIGN_NEON int8_t m_Wo[LSTM_UNITS][LSTM_UNITS];

    ALIGN_NEON int16_t m_h_state[LSTM_UNITS];
    ALIGN_NEON int16_t m_c_state[LSTM_UNITS];

    float m_fatigueIndex = 0.0f;
    float m_envLeft = 0.0f;
    float m_envRight = 0.0f;

    // FIX (tronidos / pops periódicos): el IIR de predictAndMitigateFatigue
    // usaba variables locales inicializadas a 0 en cada bloque. Cada buffer
    // empezaba con un escalón de ganancia desde cero → discontinuidad audible
    // a la frecuencia de bloque (típicamente 64-512 Hz → pop/tronido).
    // Ahora el estado persiste entre bloques como miembro de la clase.
    float m_iirStateL = 0.0f;
    float m_iirStateR = 0.0f;
};

} // namespace Ivanna
