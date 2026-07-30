#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <cmath>

namespace Ivanna {

// Tamaño de bloque de audio estándar en el pipeline
constexpr size_t BLOCK_SIZE = 256;
constexpr size_t SAMPLING_RATE = 48000;
constexpr size_t FIR_TAPS = 256;

// Aproximación rápida de tanh utilizando NEON / Newton-Raphson
inline float fast_tanh(float x) {
    float x2 = x * x;
    float a = x * (135.0f + x2 * 15.0f);
    float b = 135.0f + x2 * (60.0f + x2);
    return a / b;
}

struct alignas(16) AudioBuffer {
    std::array<float, BLOCK_SIZE> left;
    std::array<float, BLOCK_SIZE> right;
};

class IvannaFusionCore {
public:
    IvannaFusionCore();
    virtual ~IvannaFusionCore() = default;

    virtual void processBlock(AudioBuffer* buffer) = 0;
    virtual void setParameter(uint32_t paramId, float value) = 0;
};

} // namespace Ivanna
