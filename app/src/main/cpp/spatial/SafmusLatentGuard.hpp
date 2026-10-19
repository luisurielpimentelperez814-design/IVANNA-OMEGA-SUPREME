#pragma once

#include <cmath>
#include <cstddef>

namespace ivanna::safmus {

// SAFMUS boundary for the seven-dimensional SAF latent shared by HRTF and RIR.
// This deliberately validates finiteness only: parameter magnitude bounds belong
// to the model's versioned schema and must not be guessed independently here.
inline bool isFiniteLatent(const float* q) noexcept {
    if (!q) return false;
    for (std::size_t i = 0; i < 7; ++i) {
        if (!std::isfinite(q[i])) return false;
    }
    return true;
}

} // namespace ivanna::safmus
