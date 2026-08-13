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
    void setRiemannianCurvature(float curvature) { m_intrinsicCurvature.store(curvature, std::memory_order_relaxed); }

private:
    void synthesizeHrtf(float yaw, float pitch, float roll, int bank);
    std::atomic<int> m_activeBank{0};
    std::atomic<float> m_intrinsicCurvature{0.15f};
    
    ALIGN_NEON float m_hrtfLL[2][HRTF_TAPS]; // Left to Left Ear
    ALIGN_NEON float m_hrtfLR[2][HRTF_TAPS]; // Left to Right Ear (Crosstalk)
    ALIGN_NEON float m_hrtfRR[2][HRTF_TAPS]; // Right to Right Ear
    ALIGN_NEON float m_hrtfRL[2][HRTF_TAPS]; // Right to Left Ear (Crosstalk)
    
    ALIGN_NEON float m_histL[BLOCK_SIZE + HRTF_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + HRTF_TAPS];
};
} // namespace Ivanna
