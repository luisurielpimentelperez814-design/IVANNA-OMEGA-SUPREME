// ⚠ ARCHIVO LEGADO — NO EDITAR ⚠
// La fuente de verdad es app/src/main/cpp/HrtfManager.hpp
// Este archivo existe por historial pero NO se compila en ningún target.
// Cualquier cambio aquí se perderá. Edita app/src/main/cpp/.
//
#pragma once
#include "IvannaFusionCore.hpp"
#include <atomic>

namespace Ivanna {
constexpr size_t HRTF_TAPS = 128;

class HrtfManager {
public:
    HrtfManager();
    void processBinauralScene(AudioBuffer* buffer);
    void setHeadPose(float yaw, float pitch, float roll);

private:
    void synthesizeHrtf(float yaw, float pitch, float roll, int bank);
    std::atomic<int> m_activeBank{0};
    
    ALIGN_NEON float m_hrtfLL[2][HRTF_TAPS]; // Left to Left Ear
    ALIGN_NEON float m_hrtfLR[2][HRTF_TAPS]; // Left to Right Ear (Crosstalk)
    ALIGN_NEON float m_hrtfRR[2][HRTF_TAPS]; // Right to Right Ear
    ALIGN_NEON float m_hrtfRL[2][HRTF_TAPS]; // Right to Left Ear (Crosstalk)
    
    ALIGN_NEON float m_histL[BLOCK_SIZE + HRTF_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + HRTF_TAPS];
};
} // namespace Ivanna
