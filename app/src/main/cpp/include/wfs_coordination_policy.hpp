#pragma once

#include <algorithm>
#include <cmath>

namespace ivanna::unity {

// Convert the requested Acoustic Unity WFS aperture into the FusionCore
// safety scale. The consumer contract is attenuation-only [0.5, 1.0].
// Invalid adaptive values fail closed to neutral rather than propagating NaN.
inline float coordinatedWfsSafetyScale(float adaptiveScale,
                                       float requestedSpread) noexcept {
    const float adaptive = std::isfinite(adaptiveScale)
        ? std::clamp(adaptiveScale, 0.5f, 1.0f) : 1.0f;
    const float spread = std::isfinite(requestedSpread)
        ? std::clamp(requestedSpread, 0.8f, 1.35f) : 1.0f;
    return std::clamp(adaptive / std::max(1.0f, spread), 0.5f, 1.0f);
}

} // namespace ivanna::unity
