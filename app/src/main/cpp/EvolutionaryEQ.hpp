#pragma once

#include <cstddef>
#include "IvannaFusionCore.hpp"

namespace Ivanna {

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif

class EvolutionaryEQ : public IvannaFusionCore {
private:
    static constexpr size_t BANDS_512 = 512;
    
    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];
    
    // Anillo de memoria intermedia para convolución
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];

    // Algoritmo LM-CMA-ES (CMA-ES de memoria limitada)
    float m_meanGenome[BANDS_512];
    float m_evolutionPath[BANDS_512];
    float m_stepSize;

    float calculateFitness(const float* genome);
    void updateLM_CMA_ES();

public:
    EvolutionaryEQ();
    ~EvolutionaryEQ() override = default;

    void processBlock(AudioBuffer* buffer) override { processNEON(buffer); }
    void processNEON(AudioBuffer* buffer);
    void setParameter(uint32_t paramId, float value) override;
};

} // namespace Ivanna
