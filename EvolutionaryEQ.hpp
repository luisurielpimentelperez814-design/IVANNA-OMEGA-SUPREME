#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    void calibrateTargetRoom();
    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES(); // Evolution step

private:
    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];

    // Static ring buffer for zero-heap overlap-save convolution
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];

    // LM-CMA-ES evolutionary parameters
    float m_meanGenome[BANDS_512];
    float m_stepSize;
    float m_evolutionPath[BANDS_512];

    float calculateFitness(const float* genome);
};

} // namespace Ivanna
