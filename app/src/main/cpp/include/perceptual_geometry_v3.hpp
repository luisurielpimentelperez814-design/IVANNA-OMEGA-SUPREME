#pragma once

// SAFMUS / IVANNA production geometry v3.
// Bounded natural-gradient direction for a supplied Fisher-information metric.
// Wired into SaFRoomOptimizer; downstream audio-process publication uses JNI/daemon IPC.
// No Android/JNI dependencies. C++20, fixed 7-D state, no heap allocation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ivanna::perceptual_geometry_v3 {

inline constexpr std::size_t kDim = 7;
using Vector = std::array<double, kDim>;
using Matrix = std::array<std::array<double, kDim>, kDim>;

struct Result {
    Vector descent{};
    double damping = 0.0;
    double metricNorm = 0.0;
    bool valid = false;
};

inline bool allFinite(const Vector& v) noexcept {
    for (double x : v) if (!std::isfinite(x)) return false;
    return true;
}

inline bool allFinite(const Matrix& m) noexcept {
    for (const auto& row : m)
        for (double x : row) if (!std::isfinite(x)) return false;
    return true;
}

// Solve (G + damping I) x = b with Cholesky. Returns false unless the
// regularized metric is numerically symmetric positive definite.
inline bool choleskySolve(const Matrix& g, const Vector& b,
                          double damping, Vector& x) noexcept {
    Matrix l{};
    for (std::size_t i = 0; i < kDim; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = g[i][j];
            if (i == j) sum += damping;
            for (std::size_t k = 0; k < j; ++k) sum -= l[i][k] * l[j][k];
            if (i == j) {
                if (!(sum > 0.0) || !std::isfinite(sum)) return false;
                l[i][j] = std::sqrt(sum);
            } else {
                if (!(l[j][j] > 0.0)) return false;
                l[i][j] = sum / l[j][j];
            }
        }
    }

    Vector y{};
    for (std::size_t i = 0; i < kDim; ++i) {
        double sum = b[i];
        for (std::size_t j = 0; j < i; ++j) sum -= l[i][j] * y[j];
        y[i] = sum / l[i][i];
    }
    for (std::size_t rev = 0; rev < kDim; ++rev) {
        const std::size_t i = kDim - 1 - rev;
        double sum = y[i];
        for (std::size_t j = i + 1; j < kDim; ++j) sum -= l[j][i] * x[j];
        x[i] = sum / l[i][i];
    }
    return allFinite(x);
}

// Returns a descent step -G^{-1} grad, capped in the regularized Fisher norm
// sqrt(d^T (G + damping I) d). The caller must still project onto its physical
// parameter bounds and choose a safe application cadence.
// Invalid/asymmetric metrics and non-finite gradients fail closed.
inline Result boundedNaturalDescent(const Matrix& rawMetric,
                                    const Vector& gradient,
                                    double trustRadius) noexcept {
    Result out{};
    if (!allFinite(rawMetric) || !allFinite(gradient) ||
        !std::isfinite(trustRadius) || !(trustRadius > 0.0)) return out;

    double scale = 0.0;
    for (std::size_t i = 0; i < kDim; ++i) {
        scale = std::max(scale, std::abs(rawMetric[i][i]));
        for (std::size_t j = i + 1; j < kDim; ++j) {
            const double tolerance = 1.0e-10 *
                std::max({1.0, std::abs(rawMetric[i][j]), std::abs(rawMetric[j][i])});
            if (std::abs(rawMetric[i][j] - rawMetric[j][i]) > tolerance) return out;
        }
    }
    scale = std::max(scale, 1.0e-12);

    // Try no damping first. If G is positive semidefinite / ill-conditioned,
    // increase diagonal damping geometrically, but never beyond 1% of scale.
    double damping = 0.0;
    Vector solved{};
    bool solvedOk = choleskySolve(rawMetric, gradient, damping, solved);
    for (int attempt = 0; !solvedOk && attempt < 9; ++attempt) {
        damping = (attempt == 0) ? scale * 1.0e-9 : damping * 10.0;
        if (damping > scale * 1.0e-2) break;
        solved = {};
        solvedOk = choleskySolve(rawMetric, gradient, damping, solved);
    }
    if (!solvedOk) return out;

    for (std::size_t i = 0; i < kDim; ++i) out.descent[i] = -solved[i];
    double normSquared = 0.0;
    for (std::size_t i = 0; i < kDim; ++i) {
        double rowProduct = 0.0;
        for (std::size_t j = 0; j < kDim; ++j)
            rowProduct += rawMetric[i][j] * out.descent[j];
        rowProduct += damping * out.descent[i];
        normSquared += out.descent[i] * rowProduct;
    }
    if (!std::isfinite(normSquared) || normSquared < -1.0e-10) return Result{};
    out.metricNorm = std::sqrt(std::max(0.0, normSquared));
    if (out.metricNorm > trustRadius && out.metricNorm > 0.0) {
        const double factor = trustRadius / out.metricNorm;
        for (double& value : out.descent) value *= factor;
        out.metricNorm = trustRadius;
    }
    out.damping = damping;
    out.valid = allFinite(out.descent) && std::isfinite(out.metricNorm);
    return out;
}

} // namespace ivanna::perceptual_geometry_v3
