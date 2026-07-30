#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include <cstddef>
#include <cmath>
#include <random>

namespace Ivanna {

static float fast_rand() {
    static std::mt19937 gen(42);
    static std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    return dis(gen);
}

EvolutionaryEQ::EvolutionaryEQ() : m_stepSize(0.1f) {
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
    float smoothness = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        float diff = genome[i] - genome[i - 1];
        smoothness += diff * diff;
    }
    return -smoothness; 
}

void EvolutionaryEQ::updateLM_CMA_ES() {
    constexpr size_t lambda = 10;
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

    constexpr float cc = 0.1f;
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] += 0.5f * (population[best_idx][i] - m_meanGenome[i]);
        m_evolutionPath[i] = (1.0f - cc) * m_evolutionPath[i] + cc * m_meanGenome[i];
    }
}

void EvolutionaryEQ::processNEON(AudioBuffer* buffer) {
    updateLM_CMA_ES();

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

        buffer->left[i] = l_out;
        buffer->right[i] = r_out;
    }

    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

void EvolutionaryEQ::setParameter(uint32_t paramId, float value) {
    if (paramId == 0) {
        m_stepSize = value;
    }
}

} // namespace Ivanna
