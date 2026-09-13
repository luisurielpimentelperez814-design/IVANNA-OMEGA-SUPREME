#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include <cstddef>
#include <cmath>
#include <random>

namespace Ivanna {

static thread_local std::mt19937 g_rng(1337);

static inline float fast_rand() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(g_rng);
}

EvolutionaryEQ::EvolutionaryEQ() {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_firCoeffsL[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
        m_firCoeffsR[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
    }

    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] = 0.0f;
        m_evolutionPath[i] = 0.0f;
    }
}

float EvolutionaryEQ::calculateFitness(const float* genome) {
    float smoothnessPenalty = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        float diff = genome[i] - genome[i - 1];
        smoothnessPenalty += diff * diff;
    }
    return -smoothnessPenalty;
}

void EvolutionaryEQ::updateLM_CMA_ES() {
    // AUDITORÍA (frente DSP nativo, 2026-09-13): este método está
    // genuinamente incompleto, no solo desconectado —documentado aquí
    // para que quede trazable, no se conecta a ciegas:
    //
    //   1. Su único llamador real es runAcousticProfiling() (ver
    //      IvannaFusionCore.cpp), que a su vez NO tiene ningún llamador
    //      en todo el árbol — confirmado por grep exhaustivo. El
    //      optimizador nunca corre en producción hoy.
    //   2. calculateFitness() no evalúa nada relacionado con audio real
    //      — solo penaliza la varianza interna del propio genoma
    //      (diferencia entre bandas adyacentes). No hay señal de
    //      entrada, RMS, espectro ni THD de por medio: "mejor fitness"
    //      aquí solo significa "genoma más suave consigo mismo", no
    //      "mejor para el sonido".
    //   3. m_meanGenome (lo que este método sí actualiza) nunca se
    //      copia a m_firCoeffsL/m_firCoeffsR — los coeficientes que
    //      processNEON() realmente usa siguen siendo el filtro
    //      identidad fijado en el constructor, para siempre.
    //
    // Conectar (1)+(3) sin resolver (2) primero sería peor que el
    // estado actual: aplicaría un filtro FIR real al audio sin ningún
    // criterio de calidad basado en la señal — filtrado esencialmente
    // aleatorio-suavizado. Diseñar una función de fitness real (contra
    // RMS/espectro/THD de la señal de entrada) es trabajo de mayor
    // alcance que un fix puntual de discontinuidad; se deja fuera de
    // este commit a propósito.
    constexpr size_t lambda = 8;
    float population[lambda][BANDS_512];
    float fitness[lambda];

    for (size_t p = 0; p < lambda; ++p) {
        for (size_t i = 0; i < BANDS_512; ++i) {
            population[p][i] = m_meanGenome[i] + m_stepSize * fast_rand();
        }
        fitness[p] = calculateFitness(population[p]);
    }

    size_t best_idx = 0;
    float max_fit = fitness[0];
    for (size_t p = 1; p < lambda; ++p) {
        if (fitness[p] > max_fit) {
            max_fit = fitness[p];
            best_idx = p;
        }
    }

    constexpr float cc = 0.2f;
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] += 0.5f * (population[best_idx][i] - m_meanGenome[i]);
        m_evolutionPath[i] = (1.0f - cc) * m_evolutionPath[i] + cc * m_meanGenome[i];
    }
}

void EvolutionaryEQ::processNEON(Ivanna::AudioBuffer* buffer) {
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float l_out = 0.0f;
        float r_out = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            l_out += m_firCoeffsL[t] * m_histL[i + t];
            r_out += m_firCoeffsR[t] * m_histR[i + t];
        }

        buffer->left[i] = fast_tanh_scalar(l_out);
        buffer->right[i] = fast_tanh_scalar(r_out);
    }

    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
