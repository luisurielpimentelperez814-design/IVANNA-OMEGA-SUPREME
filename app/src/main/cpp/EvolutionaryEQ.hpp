#pragma once

#include <cstddef>
#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    ~EvolutionaryEQ() = default;

    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES();

private:
    alignas(16) float m_firCoeffsL[FIR_TAPS];
    alignas(16) float m_firCoeffsR[FIR_TAPS];

    alignas(16) float m_histL[BLOCK_SIZE + FIR_TAPS];
    alignas(16) float m_histR[BLOCK_SIZE + FIR_TAPS];

    float m_meanGenome[BANDS_512];
    float m_evolutionPath[BANDS_512];
    float m_stepSize{0.1f};

    float calculateFitness(const float* genome);
};

} // namespace Ivanna
