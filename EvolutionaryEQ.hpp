#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    void calibrateTargetRoom();
    void processNEON(AudioBuffer* buffer);

private:
    ALIGN_NEON float m_firCoeffs[FIR_TAPS];
    ALIGN_NEON float m_historyL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_historyR[BLOCK_SIZE + FIR_TAPS];
};

} // namespace Ivanna
