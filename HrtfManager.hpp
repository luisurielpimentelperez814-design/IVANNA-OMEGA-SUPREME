#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

constexpr size_t HRTF_TAPS = 128;

class HrtfManager {
public:
    HrtfManager();
    void processBinauralScene(AudioBuffer* buffer);

private:
    ALIGN_NEON float m_hrtfLL[HRTF_TAPS]; // Left to Left Ear
    ALIGN_NEON float m_hrtfLR[HRTF_TAPS]; // Left to Right Ear (Crosstalk)
    ALIGN_NEON float m_hrtfRR[HRTF_TAPS]; // Right to Right Ear
    ALIGN_NEON float m_hrtfRL[HRTF_TAPS]; // Right to Left Ear (Crosstalk)

    ALIGN_NEON float m_histL[BLOCK_SIZE + HRTF_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + HRTF_TAPS];
};

} // namespace Ivanna
