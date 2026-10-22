#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ivanna::supreme {

// Pure command-state contract shared by JNI and host regression tests.
// This is a bounded DSP command read-back, not a measurement of acoustic azimuth.
class SpatialAngleReadback {
public:
    static constexpr float kHalfPi = 1.57079632679489661923f;

    // Non-finite requests return NaN so the caller can preserve the last valid command.
    static float appliedRadians(float requestedRadians) noexcept {
        if (!std::isfinite(requestedRadians)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return std::clamp(requestedRadians, -kHalfPi, kHalfPi);
    }
};

} // namespace ivanna::supreme
