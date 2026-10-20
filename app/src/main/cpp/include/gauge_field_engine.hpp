#pragma once

// Bounded Gauge-Field Engine (GFE) primitive for complex baseband / analytic audio.
// This is a userspace DSP primitive, not a kernel IRQ/DAC hook. Berry phase is
// estimated from overlaps between normalized complex states supplied by the
// caller; it is not inferred from ordinary real PCM samples automatically.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace ivanna::gfe {

inline constexpr std::size_t kLutSize = 65536;
inline constexpr std::size_t kFirTaps = 128;
inline constexpr double kPi = 3.141592653589793238462643383279502884;
inline constexpr double kTwoPi = 2.0 * kPi;

using Complex = std::complex<float>;
using State = std::array<Complex, 7>;

struct Status {
    bool valid = false;
    double berryPhase = 0.0;
    std::uint16_t lutIndex = 0;
};

class BerryPhaseProjector {
public:
    BerryPhaseProjector() noexcept {
        for (std::size_t i = 0; i < kLutSize; ++i) {
            const double phase = kTwoPi * static_cast<double>(i) /
                                 static_cast<double>(kLutSize);
            m_lut[i] = Complex(static_cast<float>(std::cos(phase)),
                                static_cast<float>(std::sin(phase)));
        }
    }

    // Advance geometric phase by arg(<previous|current>) for a closed sequence
    // of nonzero states. Inputs are normalized internally; invalid inputs fail
    // closed and do not change the previously accepted state or phase.
    Status observe(const State& state) noexcept {
        double norm2 = 0.0;
        for (const auto& z : state) {
            if (!std::isfinite(z.real()) || !std::isfinite(z.imag())) return status();
            norm2 += static_cast<double>(std::norm(z));
        }
        if (!std::isfinite(norm2) || norm2 <= 1.0e-20) return status();

        State normalized{};
        const float invNorm = static_cast<float>(1.0 / std::sqrt(norm2));
        for (std::size_t i = 0; i < state.size(); ++i) normalized[i] = state[i] * invNorm;

        if (m_initialized) {
            Complex overlap{};
            for (std::size_t i = 0; i < state.size(); ++i)
                overlap += std::conj(m_previous[i]) * normalized[i];
            const double magnitude = std::abs(overlap);
            if (!std::isfinite(magnitude) || magnitude <= 1.0e-12) return status();
            m_phase = std::remainder(m_phase + std::arg(overlap), kTwoPi);
        }
        m_previous = normalized;
        m_initialized = true;
        return status();
    }

    // Rotate complex analytic/IQ samples only. A real scalar PCM sample cannot
    // receive an arbitrary phase rotation without an analytic-signal stage.
    Complex project(Complex sample) const noexcept {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) return {};
        return sample * std::conj(m_lut[indexFor(m_phase)]);
    }

    double phase() const noexcept { return m_phase; }
    std::uint16_t lutIndex() const noexcept { return indexFor(m_phase); }

private:
    static std::uint16_t indexFor(double phase) noexcept {
        double wrapped = std::fmod(phase, kTwoPi);
        if (wrapped < 0.0) wrapped += kTwoPi;
        const auto index = static_cast<std::size_t>(
            wrapped * static_cast<double>(kLutSize) / kTwoPi);
        return static_cast<std::uint16_t>(std::min(index, kLutSize - 1));
    }

    Status status() const noexcept {
        return {m_initialized, m_phase, indexFor(m_phase)};
    }

    std::array<Complex, kLutSize> m_lut{};
    State m_previous{};
    double m_phase = 0.0;
    bool m_initialized = false;
};

// Fixed-size, allocation-free complex FIR. Caller supplies calibrated taps;
// this class deliberately does not invent a 128-tap room/HRTF response.
class Fir128 {
public:
    using Taps = std::array<Complex, kFirTaps>;
    explicit Fir128(const Taps& taps) noexcept : m_taps(taps) {}

    Complex process(Complex input) noexcept {
        if (!std::isfinite(input.real()) || !std::isfinite(input.imag())) input = {};
        m_history[m_head] = input;
        Complex output{};
        std::size_t idx = m_head;
        for (std::size_t tap = 0; tap < kFirTaps; ++tap) {
            output += m_taps[tap] * m_history[idx];
            idx = idx == 0 ? kFirTaps - 1 : idx - 1;
        }
        m_head = (m_head + 1) % kFirTaps;
        if (!std::isfinite(output.real()) || !std::isfinite(output.imag())) return {};
        return output;
    }

    void reset() noexcept { m_history.fill(Complex{}); m_head = 0; }

private:
    Taps m_taps{};
    std::array<Complex, kFirTaps> m_history{};
    std::size_t m_head = 0;
};

} // namespace ivanna::gfe
