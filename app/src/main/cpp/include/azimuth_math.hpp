#pragma once

#include <cmath>

namespace ivanna::spatial {

// Preserve the full spherical azimuth. Clamping to +/-90 degrees collapses
// every source in the rear hemisphere onto a side-facing position.
inline float wrapAzimuthDegrees(float degrees) noexcept {
    if (!std::isfinite(degrees)) return 0.0f;
    return std::remainder(degrees, 360.0f);
}

} // namespace ivanna::spatial
