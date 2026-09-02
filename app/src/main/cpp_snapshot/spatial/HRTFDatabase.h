#pragma once

#include <cstddef>
#include <array>

namespace Ivanna {

constexpr size_t HRTF_TAPS = 128;
constexpr size_t HRTF_AZIMUTHS = 24;  // Cada 15 grados (0..360)
constexpr size_t HRTF_ELEVATIONS = 7; // -45, -30, -15, 0, 15, 30, 45 grados

struct HRTFImpulsePair {
    float left[HRTF_TAPS];
    float right[HRTF_TAPS];
};

extern const HRTFImpulsePair g_hrtfDatabase[HRTF_ELEVATIONS][HRTF_AZIMUTHS];

} // namespace Ivanna
