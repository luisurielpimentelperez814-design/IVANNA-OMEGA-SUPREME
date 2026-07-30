#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 512;
constexpr float SAMPLE_RATE = 48000.0f;

struct alignas(16) AudioBuffer {
    float left[BLOCK_SIZE];
    float right[BLOCK_SIZE];
};

class IvannaFusionCore {
public:
    IvannaFusionCore();
    ~IvannaFusionCore() = default;

    void setParameters(float targetGainDb, float compThreshDb, float compRatio, 
                       float exciteEven, float exciteOdd, float lowPassCutoff) noexcept;

    void processBlock(AudioBuffer* buffer) noexcept;

private:
    float m_targetGainLinear = 1.0f;
    float m_compThreshLinear = 0.125f;
    float m_compRatio = 2.0f;
    float m_exciteEven = 0.1f;
    float m_exciteOdd = 0.05f;
    float m_lowPassAlpha = 0.95f;

    // Filter states
    float m_lpStateL = 0.0f;
    float m_lpStateR = 0.0f;

    // Peak Limiter state
    float m_limiterEnvL = 0.0f;
    float m_limiterEnvR = 0.0f;
};

} // namespace Ivanna
