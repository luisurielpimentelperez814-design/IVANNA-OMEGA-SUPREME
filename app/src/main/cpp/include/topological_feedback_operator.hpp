#pragma once

// SAFMUS adaptive feedback geometry for the 7-D latent control plane.
// This is a bounded residual-geometry controller, NOT a claim to compute
// Ricci curvature or a statistically calibrated Fisher-information tensor.
// It runs on the optimizer/control path, never inside the audio callback.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ivanna::topological_feedback {

inline constexpr std::size_t kDim = 7;
using Vector = std::array<double, kDim>;

struct State {
    Vector metricScale{};
    double trustRadius = 0.10;
    double anisotropy = 1.0;
    bool valid = false;
};

class Operator {
public:
    Operator() noexcept { m_ema.fill(0.0); }

    // residual is target - current, in the actual bounded latent coordinates.
    // Invalid observations are rejected and leave the last safe state unchanged.
    State observe(const Vector& residual) noexcept {
        for (double value : residual) {
            if (!std::isfinite(value)) return m_state;
        }

        constexpr double beta = 0.10;
        constexpr double floor = 1.0e-12;
        double mean = 0.0;
        for (std::size_t i = 0; i < kDim; ++i) {
            const double energy = residual[i] * residual[i];
            if (!std::isfinite(energy)) return m_state;
            m_ema[i] = m_initialized ? (1.0 - beta) * m_ema[i] + beta * energy : energy;
            mean += m_ema[i];
        }
        mean /= static_cast<double>(kDim);
        if (!std::isfinite(mean)) return m_state;

        double minEnergy = m_ema[0] + floor;
        double maxEnergy = minEnergy;
        for (double energy : m_ema) {
            minEnergy = std::min(minEnergy, energy + floor);
            maxEnergy = std::max(maxEnergy, energy + floor);
        }
        const double ratio = std::clamp(minEnergy / maxEnergy, 0.0, 1.0);
        // Bounded diagonal reweighting prevents one latent axis from dominating
        // the trust metric; all scales remain positive and well-conditioned.
        for (std::size_t i = 0; i < kDim; ++i) {
            const double relative = (m_ema[i] + floor) / (mean + floor);
            m_state.metricScale[i] = std::clamp(std::sqrt(relative), 0.5, 2.0);
        }
        m_state.anisotropy = maxEnergy / minEnergy;
        // Reduce the allowed step when residual energy is strongly anisotropic.
        m_state.trustRadius = std::clamp(0.025 + 0.075 * std::sqrt(ratio), 0.025, 0.10);
        m_state.valid = std::isfinite(m_state.anisotropy);
        if (!m_state.valid) {
            m_state.metricScale.fill(1.0);
            m_state.trustRadius = 0.025;
            m_state.anisotropy = 1.0;
        }
        m_initialized = true;
        return m_state;
    }

    State state() const noexcept { return m_state; }

private:
    Vector m_ema{};
    State m_state{};
    bool m_initialized = false;
};

} // namespace ivanna::topological_feedback
