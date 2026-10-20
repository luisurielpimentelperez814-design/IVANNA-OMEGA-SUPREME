#pragma once

// SAFMUS production latent transport: bounded 1-D Wasserstein-2
// displacement interpolation over the ordered SAF/PCA mode index.
// Fixed-size, allocation-free and deterministic for the 7-D latent state.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ivanna::wasserstein_latent_flow {
inline constexpr std::size_t kDim = 7;
using Vector = std::array<double, kDim>;

struct Result {
    Vector target{};
    double sourceMass = 0.0;
    double targetMass = 0.0;
    double transportedMass = 0.0;
    bool valid = false;
};

inline bool finite(const Vector& v) noexcept {
    for (double x : v) if (!std::isfinite(x)) return false;
    return true;
}

// Monotone optimal coupling of |latent| distributions on support {0,...,6}.
// Signs stay attached to target coordinates. This defines a proximal target
// trajectory; PCA index is not claimed to be a calibrated frequency axis.
inline Result transportTarget(const Vector& current, const Vector& target,
                              double fraction = 0.15) noexcept {
    Result out{};
    if (!finite(current) || !finite(target) || !std::isfinite(fraction) ||
        fraction < 0.0 || fraction > 1.0) return out;

    std::array<double, kDim> a{}, b{}, transported{};
    for (std::size_t i = 0; i < kDim; ++i) {
        a[i] = std::abs(current[i]);
        b[i] = std::abs(target[i]);
        out.sourceMass += a[i];
        out.targetMass += b[i];
    }
    if (!(out.targetMass > 1.0e-15)) {
        out.target = current;
        out.valid = true;
        return out;
    }
    if (!(out.sourceMass > 1.0e-15) || fraction == 1.0) {
        out.target = target;
        out.transportedMass = out.targetMass;
        out.valid = true;
        return out;
    }
    for (double& x : a) x /= out.sourceMass;
    for (double& x : b) x /= out.targetMass;

    std::size_t i = 0, j = 0;
    double remA = a[0], remB = b[0];
    constexpr double eps = 1.0e-14;
    while (i < kDim && j < kDim) {
        const double mass = std::min(remA, remB);
        if (mass > 0.0) {
            const double x = (1.0 - fraction) * static_cast<double>(i)
                           + fraction * static_cast<double>(j);
            const auto lo = static_cast<std::size_t>(std::clamp(
                static_cast<int>(std::floor(x)), 0, static_cast<int>(kDim - 1)));
            const auto hi = std::min(lo + 1, kDim - 1);
            const double upper = x - static_cast<double>(lo);
            transported[lo] += mass * (1.0 - upper);
            transported[hi] += mass * upper;
        }
        remA -= mass;
        remB -= mass;
        if (remA <= eps) {
            ++i;
            if (i < kDim) remA = a[i];
        }
        if (remB <= eps) {
            ++j;
            if (j < kDim) remB = b[j];
        }
    }

    double sum = 0.0;
    for (double x : transported) sum += x;
    if (!std::isfinite(sum) || !(sum > 0.0)) return Result{};
    for (std::size_t k = 0; k < kDim; ++k) {
        const double sign = target[k] < 0.0 ? -1.0 :
                            target[k] > 0.0 ? 1.0 :
                            (current[k] < 0.0 ? -1.0 : 1.0);
        out.target[k] = sign * transported[k] / sum * out.targetMass;
    }
    out.transportedMass = sum * out.targetMass;
    out.valid = finite(out.target);
    return out;
}
} // namespace ivanna::wasserstein_latent_flow
