#pragma once

// SAFMUS / IVANNA v2: exact constrained MAP solution for the declared
// quadratic objective. Header-only, C++20, no Android/JNI dependencies.
// This is deliberately a quadratic estimator, not a claim to solve arbitrary
// non-convex perceptual objectives.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ivanna::perceptual_v2 {

inline constexpr std::size_t kDim = 7;

struct Box {
    std::array<float, kDim> lower{};
    std::array<float, kDim> upper{};
};

struct Context {
    float rt60Seconds = 0.3f;       // [0, 5]
    float hrtfMismatchEnergy = 0;   // [0, 1]
    float diffuseness = 0;          // [0, 1]
};

struct Result {
    std::array<float, kDim> params{};
    float lambda = 0;
    float contextExponent = 0;
    double objective = 0;
    double projectedKktResidual = 0;
    bool finite = false;
};

inline float clampFinite(float value, float fallback, float lo, float hi) noexcept {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

inline float contextExponent(const Context& raw) noexcept {
    const float rt60 = clampFinite(raw.rt60Seconds, 0.3f, 0.0f, 5.0f);
    const float mismatch = clampFinite(raw.hrtfMismatchEnergy, 0.0f, 0.0f, 1.0f);
    const float diffuse = clampFinite(raw.diffuseness, 0.0f, 0.0f, 1.0f);
    // Coefficients are inherited from the existing SAF-Room context model.
    // HRTF input is already a normalized mismatch-energy scalar.
    return 0.15f * rt60 + mismatch + 0.40f * diffuse;
}

inline Result solveMapQuadratic(
        const std::array<float, kDim>& target,
        const std::array<float, kDim>& prior,
        const Box& box,
        const Context& context,
        float lambda0 = 0.01f) noexcept {
    Result result{};
    const float exponent = std::clamp(contextExponent(context), 0.0f, 2.15f);
    const float safeLambda0 = clampFinite(lambda0, 0.01f, 1.0e-6f, 1.0e3f);
    const float lambda = safeLambda0 * std::exp(exponent);
    result.lambda = lambda;
    result.contextExponent = exponent;
    result.finite = std::isfinite(lambda) && lambda > 0.0f;
    if (!result.finite) return result;

    double objective = 0.0;
    double kktSquared = 0.0;
    for (std::size_t i = 0; i < kDim; ++i) {
        const float lo = std::isfinite(box.lower[i]) ? box.lower[i] : -1.0f;
        const float hi = std::isfinite(box.upper[i]) ? box.upper[i] : 1.0f;
        if (!(lo <= hi) || !std::isfinite(target[i]) || !std::isfinite(prior[i])) {
            result.finite = false;
            return result;
        }

        // J_i(p) = 1/2 (p - target)^2 + lambda/2 (p - prior)^2.
        // Its unique unconstrained minimizer is exact; projection onto a box
        // is coordinate-wise clipping, so this is also the exact constrained
        // minimizer for the diagonal quadratic objective.
        const double unconstrained =
                (static_cast<double>(target[i]) +
                 static_cast<double>(lambda) * prior[i]) /
                (1.0 + static_cast<double>(lambda));
        const float p = static_cast<float>(std::clamp(
                unconstrained, static_cast<double>(lo), static_cast<double>(hi)));
        result.params[i] = p;

        const double dt = static_cast<double>(p) - target[i];
        const double dp = static_cast<double>(p) - prior[i];
        objective += 0.5 * dt * dt + 0.5 * lambda * dp * dp;

        const double gradient = dt + lambda * dp;
        const double projected = std::clamp(
                static_cast<double>(p) - gradient,
                static_cast<double>(lo), static_cast<double>(hi));
        const double residual = static_cast<double>(p) - projected;
        kktSquared += residual * residual;
        result.finite = result.finite && std::isfinite(p) &&
                        std::isfinite(objective) && std::isfinite(kktSquared);
    }
    result.objective = objective;
    result.projectedKktResidual = std::sqrt(kktSquared);
    return result;
}

} // namespace ivanna::perceptual_v2
