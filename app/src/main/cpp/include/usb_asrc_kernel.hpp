#pragma once

#include <cmath>
#include <cstddef>
#include "usb_asrc_cutoff.hpp"

namespace ivanna::audio {

// Build the exact polyphase FIR used by the USB ingress ASRC. Kept in a
// host-testable header so spectral regressions exercise production coefficients.
template <std::size_t Phases, std::size_t Taps>
void buildUsbAsrcKernel(int inputRate, int outputRate,
                        float (&kernel)[Phases][Taps]) noexcept {
    constexpr double kPi = 3.14159265358979323846;
    // Helper returns cutoff as a fraction of Nyquist (Nyquist = 1); sinc() below
    // is normalized in cycles/sample (Nyquist = 0.5), so convert units here.
    const double cutoff = 0.5 * usbAsrcCutoffNyquistNormalized(inputRate, outputRate);
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
