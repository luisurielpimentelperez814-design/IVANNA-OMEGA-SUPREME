#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class HrtfManager {
public:
    HrtfManager();
    void processSpatialHrtf(AudioBuffer* buffer, float azimuth, float elevation);

private:
    ALIGN_NEON float m_hrtfLL[FIR_TAPS];
    ALIGN_NEON float m_hrtfRL[FIR_TAPS];
    ALIGN_NEON float m_hrtfRR[FIR_TAPS];
    ALIGN_NEON float m_hrtfLR[FIR_TAPS];
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];
};

} // namespace Ivanna
