#pragma once
// Second-order software PLL for phase tracking in userspace DSP.
// The reference is a measured phase (radians), not an OS timestamp.
// This estimates phase/frequency; it does not clock or discipline the physical DAC.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ivanna::gfe {
inline constexpr double kPllPi = 3.141592653589793238462643383279502884;
inline constexpr double kPllTwoPi = 2.0 * kPllPi;

struct PllStatus {
    bool valid = false;
    double phaseRadians = 0.0;
    double frequencyRadiansPerSecond = 0.0;
    double phaseErrorRadians = 0.0;
};

class GeometricPll {
public:
    // naturalFrequencyHz controls loop bandwidth; damping=1 is critically damped
    // for the linearized continuous-time model. maxCorrection bounds frequency pull.
    explicit GeometricPll(double nominalFrequencyHz,
                          double naturalFrequencyHz = 3.0,
                          double damping = 1.0,
                          double maxCorrectionHz = 100.0) noexcept {
        configure(nominalFrequencyHz, naturalFrequencyHz, damping, maxCorrectionHz);
    }

    bool configure(double nominalFrequencyHz, double naturalFrequencyHz,
                   double damping, double maxCorrectionHz) noexcept {
        if (!finitePositive(nominalFrequencyHz) || !finitePositive(naturalFrequencyHz) ||
            !finitePositive(damping) || !finitePositive(maxCorrectionHz) ||
            naturalFrequencyHz >= nominalFrequencyHz) {
            m_configured = false;
            return false;
        }
        m_nominalOmega = kPllTwoPi * nominalFrequencyHz;
        const double wn = kPllTwoPi * naturalFrequencyHz;
        // Type-II PI loop with phase detector gain and NCO gain normalized to 1.
        m_kp = 2.0 * damping * wn;
        m_ki = wn * wn;
        m_maxCorrectionOmega = kPllTwoPi * maxCorrectionHz;
        m_configured = true;
        reset();
        return true;
    }

    void reset(double initialPhaseRadians = 0.0) noexcept {
        m_phase = std::isfinite(initialPhaseRadians) ? wrap(initialPhaseRadians) : 0.0;
        m_integral = 0.0;
        m_lastError = 0.0;
        m_initialized = false;
    }

    // dtSeconds must be the elapsed time between phase observations. First sample
    // establishes phase; later samples update a bounded PI estimate. No allocations,
    // locks, syscalls, or dynamic memory occur here.
    PllStatus update(double referencePhaseRadians, double dtSeconds) noexcept {
        if (!m_configured || !std::isfinite(referencePhaseRadians) ||
            !finitePositive(dtSeconds) || dtSeconds > 1.0) return status(false);

        const double reference = wrap(referencePhaseRadians);
        if (!m_initialized) {
            m_phase = reference;
            m_initialized = true;
            m_lastError = 0.0;
            return status(true);
        }

        const double error = wrap(reference - m_phase);
        // Integrator anti-windup by clamping the integral's frequency contribution.
        const double integralLimit = m_maxCorrectionOmega / m_ki;
        m_integral = std::clamp(m_integral + error * dtSeconds,
                                -integralLimit, integralLimit);
        const double correction = std::clamp(m_kp * error + m_ki * m_integral,
                                             -m_maxCorrectionOmega, m_maxCorrectionOmega);
        m_phase = wrap(m_phase + (m_nominalOmega + correction) * dtSeconds);
        m_lastError = error;
        return status(true);
    }

    double phaseRadians() const noexcept { return m_phase; }
    double nominalFrequencyHz() const noexcept { return m_nominalOmega / kPllTwoPi; }

private:
    static bool finitePositive(double x) noexcept { return std::isfinite(x) && x > 0.0; }
    static double wrap(double x) noexcept {
        double y = std::remainder(x, kPllTwoPi);
        return y <= -kPllPi ? y + kPllTwoPi : y;
    }
    PllStatus status(bool valid) const noexcept {
        // Report the same bounded correction actually applied by update().
        // Previously telemetry could claim a frequency pull beyond the configured
        // limit even though the phase accumulator itself was clamped.
        const double correction = std::clamp(m_kp * m_lastError + m_ki * m_integral,
                                             -m_maxCorrectionOmega, m_maxCorrectionOmega);
        return {valid && m_initialized, m_phase,
                (m_nominalOmega + correction) / kPllTwoPi, m_lastError};
    }

    double m_nominalOmega = 0.0, m_kp = 0.0, m_ki = 0.0;
    double m_maxCorrectionOmega = 0.0, m_phase = 0.0, m_integral = 0.0, m_lastError = 0.0;
    bool m_configured = false, m_initialized = false;
};
} // namespace ivanna::gfe
