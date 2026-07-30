#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <cmath>

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 128;
constexpr size_t FIR_TAPS = 256;
constexpr size_t BANDS_512 = 512;

struct AudioBuffer {
    alignas(16) float left[BLOCK_SIZE];
    alignas(16) float right[BLOCK_SIZE];
};

inline float fast_tanh_neon(float x) {
    float x2 = x * x;
    float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
}

} // namespace Ivanna
