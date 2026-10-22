#pragma once

#include <cmath>
#include <cstddef>
#include "usb_asrc_cutoff.hpp"

namespace ivanna::audio {

enum class UsbAsrcPhaseDisposition {
    Ready,
    DeferToNextBlock,
    Invalid
};

// Convert a fractional source position to the rounded polyphase LUT index.
// Rounding the last fraction up can move base to inputFrames; that output must
// be deferred to the next block instead of reading input[inputFrames].
inline UsbAsrcPhaseDisposition normalizeUsbAsrcPhase(
        double phase, int inputFrames, int phases,
        int& base, int& phaseIndex) noexcept {
    if (!std::isfinite(phase) || phase < 0.0 || inputFrames <= 0 || phases <= 0)
        return UsbAsrcPhaseDisposition::Invalid;
    // Guard before floating-to-integer conversion: an extreme but finite phase
    // cannot be represented by int and must never trigger undefined behavior.
    if (phase >= static_cast<double>(inputFrames)) {
        base = inputFrames;
        phaseIndex = 0;
        return UsbAsrcPhaseDisposition::DeferToNextBlock;
    }
    base = static_cast<int>(phase);
    const double frac = phase - static_cast<double>(base);
    phaseIndex = static_cast<int>(frac * static_cast<double>(phases) + 0.5);
    if (phaseIndex >= phases) {
        ++base;
        phaseIndex = 0;
    }
    return base >= inputFrames
        ? UsbAsrcPhaseDisposition::DeferToNextBlock
        : UsbAsrcPhaseDisposition::Ready;
}

// Build the exact polyphase FIR used by the USB ingress ASRC. Kept in a
// host-testable header so spectral regressions exercise production coefficients.
template <std::size_t Phases, std::size_t Taps>
void buildUsbAsrcKernel(int inputRate, int outputRate,
                        float (&kernel)[Phases][Taps]) noexcept {
    constexpr double kPi = 3.14159265358979323846;
    // cutoff is normalized to Nyquist (1.0); actual cycles/sample cutoff is cutoff/2.
    // The ideal impulse is cutoff*sinc(cutoff*d), equivalent to
    // 2*f*sinc(2*f*d) with f=cutoff/2 and sinc(x)=sin(pi*x)/(pi*x).
    const double cutoff = usbAsrcCutoffNyquistNormalized(inputRate, outputRate);
    if (!(cutoff > 0.0) || !std::isfinite(cutoff)) {
        for (std::size_t p = 0; p < Phases; ++p)
            for (std::size_t k = 0; k < Taps; ++k) kernel[p][k] = 0.0f;
        return;
    }

    constexpr double center = (static_cast<double>(Taps) - 1.0) * 0.5;
    for (std::size_t p = 0; p < Phases; ++p) {
        const double frac = static_cast<double>(p) / static_cast<double>(Phases);
        double sum = 0.0;
        for (std::size_t k = 0; k < Taps; ++k) {
            const double d = static_cast<double>(k) + frac - center;
            const double norm = d / (center + 0.5);
            const double window = (std::abs(norm) >= 1.0) ? 0.0 :
                0.35875 + 0.48829 * std::cos(kPi * norm) +
                0.14128 * std::cos(2.0 * kPi * norm) +
                0.01168 * std::cos(3.0 * kPi * norm);
            const double c = cutoff * (std::abs(cutoff * d) < 1.0e-12
                ? 1.0 : std::sin(kPi * cutoff * d) / (kPi * cutoff * d)) * window;
            kernel[p][k] = static_cast<float>(c);
            sum += c;
        }
        if (std::abs(sum) > 1.0e-12) {
            for (std::size_t k = 0; k < Taps; ++k)
                kernel[p][k] = static_cast<float>(kernel[p][k] / sum);
        } else {
            for (std::size_t k = 0; k < Taps; ++k) kernel[p][k] = 0.0f;
        }
    }
}

} // namespace ivanna::audio
