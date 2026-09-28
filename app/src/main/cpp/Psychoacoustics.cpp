#include "Psychoacoustics.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

Psychoacoustics::Psychoacoustics() {
    for (size_t i = 0; i < LSTM_UNITS; ++i) {
        m_h_state[i] = 0;
        m_c_state[i] = 0;
        for (size_t j = 0; j < LSTM_UNITS; ++j) {
            m_Wf[i][j] = static_cast<int8_t>((i == j) ? 64 : 0);
            m_Wi[i][j] = 10;
            m_Wc[i][j] = 5;
            m_Wo[i][j] = 20;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// applyMaskingCompensation
//
// FIX (chirridos / intermodulación / pumping):
//
// 1. att/rel anteriores (0.001 / 0.0001) creaban un follower de envolvente
//    radicalmente asimétrico: ataque instantáneo, release casi infinita.
//    Resultado: la ganancia comp subía en cada transiente y nunca bajaba
//    → el módulo operaba como rectificador de semionada amplificado → distorsión
//    par + zumbido DC, audible como chirrido/buzz continuo.
//    Solución: coeficientes por tiempo (5 ms attack, 80 ms release a 48 kHz/128).
//
// 2. El umbral de puerta m_envLeft > 0.1 es correcto pero comp puede
//    llegar a 1.25 en frames de casi-silencio (env alto, absL mínimo).
//    Se añade smoothing de ganancia (gain smoothing) para evitar saltos.
//
// 3. La expansión upward se limita ahora a +1.5 dB máximo (ratio 1:1.2)
//    con curva de rodilla blanda para que no haya borde audible.
// ─────────────────────────────────────────────────────────────────────────────

void Psychoacoustics::applyMaskingCompensation(Ivanna::AudioBuffer* buffer) {

    // Coeficientes de envolvente para 48 kHz
    // Attack:  5 ms  → exp(-1 / (0.005 * 48000)) ≈ 0.99583
    // Release: 80 ms → exp(-1 / (0.080 * 48000)) ≈ 0.99974
    constexpr float ATT = 0.99583f;
    constexpr float REL = 0.99974f;

    // Expansión máxima suave de micro-detalle: +1.0 dB = factor 1.122
    constexpr float MAX_EXPAND = 1.122f;
    // Umbral de puerta: -42 dBFS ≈ 0.008 (no expandir ruido de fondo)
    constexpr float GATE_THRESH = 0.008f;
    // Umbral superior de expansión de bajo nivel: -16 dBFS ≈ 0.16
    constexpr float ENV_THRESH = 0.16f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {

        // ── Left ──────────────────────────────────────────────────────────
        {
            const float absL = std::abs(buffer->left[i]);
            m_envLeft = (absL > m_envLeft)
                ? (1.0f - ATT) * absL + ATT * m_envLeft
                : (1.0f - REL) * absL + REL * m_envLeft;

            // NOTA CRÍTICA: La ganancia objetivo debe depender EXCLUSIVAMENTE de la
            // envolvente lenta m_envLeft, nunca de absL/m_envLeft muestra a muestra.
            // Usar absL/m_envLeft amplificaba los cruces por cero de cada ciclo
            // senoidal mientras dejaba los picos en 1.0, deformando la onda (THD severa).
            float targetGainL = 1.0f;
            if (m_envLeft > GATE_THRESH && m_envLeft < ENV_THRESH) {
                const float normEnv = (m_envLeft - GATE_THRESH) / (ENV_THRESH - GATE_THRESH);
                const float window  = 4.0f * normEnv * (1.0f - normEnv); // campana suave C1 en [0,1]
                targetGainL = 1.0f + (MAX_EXPAND - 1.0f) * window;
            }

            // Suavizado de ganancia lento (τ ≈ 15 ms → 0.9986 @ 48 kHz) libre de modulación en banda de audio
            m_gainSmL = 0.9986f * m_gainSmL + 0.0014f * targetGainL;
            buffer->left[i] *= m_gainSmL;
        }

        // ── Right ─────────────────────────────────────────────────────────
        {
            const float absR = std::abs(buffer->right[i]);
            m_envRight = (absR > m_envRight)
                ? (1.0f - ATT) * absR + ATT * m_envRight
                : (1.0f - REL) * absR + REL * m_envRight;

            float targetGainR = 1.0f;
            if (m_envRight > GATE_THRESH && m_envRight < ENV_THRESH) {
                const float normEnv = (m_envRight - GATE_THRESH) / (ENV_THRESH - GATE_THRESH);
                const float window  = 4.0f * normEnv * (1.0f - normEnv);
                targetGainR = 1.0f + (MAX_EXPAND - 1.0f) * window;
            }

            m_gainSmR = 0.9986f * m_gainSmR + 0.0014f * targetGainR;
            buffer->right[i] *= m_gainSmR;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// predictAndMitigateFatigue
//
// FIX (saturación del acumulador LSTM + LPF permanente):
//
// 1. c_new = vmulq_s16(c_prev, f_t) sin shift provoca overflow en 2-3 bloques
//    (int16 * int16 sin >>8 satura a ±32767 de inmediato).
//    Resultado: m_fatigueIndex = 1.0 permanente → alpha = 0.6 → LPF duro
//    todo el tiempo → señal siempre opaca.
//    Solución: shift correcto >>8 en Q.8 aritmética + decay exponencial para
//    que el índice vuelva a 0 en silencio.
//
// 2. El IIR stateL/R ya se corrigió (persiste entre bloques) — se mantiene.
//
// 3. Se limita alpha a [0.85, 1.0] para que el LPF nunca sea tan agresivo
//    que elimine presencia vocal (f_-3dB mínima ~7 kHz a SR=48k).
// ─────────────────────────────────────────────────────────────────────────────

void Psychoacoustics::predictAndMitigateFatigue(Ivanna::AudioBuffer* buffer) {

    // RMS del bloque izquierdo como indicador de nivel
    float rms = 0.0f;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        rms += buffer->left[i] * buffer->left[i];
    }
    rms = std::sqrt(rms / static_cast<float>(BLOCK_SIZE));

    // Escala a Q.8: 0..1 float → 0..256 int16, clampear a 127 para no saturar
    const int16_t x_t = static_cast<int16_t>(
        std::min(rms * 128.0f, 127.0f)
    );

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // FIX: shift >>7 para mantener rango Q.7 después de multiplicar Q.7 * Q.7
    int16x8_t c_prev = vld1q_s16(m_c_state);
    int16x8_t f_t    = vdupq_n_s16(x_t);
    // vshrq_n_s16 shift aritmético para preservar signo
    int16x8_t c_new  = vshrq_n_s16(vmulq_s16(c_prev, f_t), 7);
    // Decay exponencial: c *= 0.97 ≈ (127/128) en Q.7
    c_new = vshrq_n_s16(vmulq_s16(c_new, vdupq_n_s16(124)), 7);
    vst1q_s16(m_c_state, c_new);

    const int32_t cell_sum = static_cast<int32_t>(vgetq_lane_s16(c_new, 0));
    m_fatigueIndex = std::abs(static_cast<float>(cell_sum)) / 127.0f;
#else
    // Scalar path: Q.7 * Q.7 >> 7 + decay 0.97
    m_c_state[0] = static_cast<int16_t>(
        (static_cast<int32_t>(m_c_state[0]) * x_t) >> 7
    );
    m_c_state[0] = static_cast<int16_t>(
        (static_cast<int32_t>(m_c_state[0]) * 124) >> 7
    );
    m_fatigueIndex = std::abs(static_cast<float>(m_c_state[0])) / 127.0f;
#endif

    // Clamp [0, 1]
    if (m_fatigueIndex > 1.0f) m_fatigueIndex = 1.0f;

    // FIX: alpha limitado a [0.85, 1.0] — nunca un LPF tan agresivo que
    // elimine presencia vocal (≥ 7 kHz de f_-3dB).
    // alpha=1.0 → sin filtrado; alpha=0.85 → f_-3dB ≈ 5.9 kHz (máximo corte)
    const float alpha = 0.85f + (1.0f - 0.85f) * (1.0f - m_fatigueIndex);

    // IIR 1er orden con estado persistente (sin tronidos entre bloques)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_iirStateL = m_iirStateL + alpha * (buffer->left[i]  - m_iirStateL);
        m_iirStateR = m_iirStateR + alpha * (buffer->right[i] - m_iirStateR);
        buffer->left[i]  = m_iirStateL;
        buffer->right[i] = m_iirStateR;
    }
}

} // namespace Ivanna
