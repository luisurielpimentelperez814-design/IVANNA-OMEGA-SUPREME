#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ivanna::supreme {

// Pure command-state contract shared by the JNI setter and host regression tests.
// This is a bounded DSP command read-back, not a measurement of acoustic azimuth.
class SpatialAngleReadback {
public:
    static constexpr float kHalfPi = 1.57079632679489661923f;

    // Returns NaN for non-finite commands so callers can fail closed without
    // overwriting the last valid DSP state. Valid requests are clamped to ±90°,
    // matching CueBasedSpatial::set_spatial_angle's supported command range.
    static float appliedRadians(float requestedRadians) noexcept {
        if (!std::isfinite(requestedRadians)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return std::clamp(requestedRadians, -kHalfPi, kHalfPi);
    }
};

} // namespace ivanna::supreme
