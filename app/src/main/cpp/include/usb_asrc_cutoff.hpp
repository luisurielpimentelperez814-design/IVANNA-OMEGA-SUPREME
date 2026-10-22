#pragma once

// Frequency normalization for UsbIngressAsrc's sinc(x)=sin(pi*x)/(pi*x).
// The kernel uses cutoff normalized to Nyquist (Nyquist = 1.0; 0.5 cycles/sample).
namespace ivanna::audio {

constexpr double usbAsrcCutoffNyquistNormalized(int inputRate, int outputRate) noexcept {
    if (inputRate <= 0 || outputRate <= 0) return 0.0;
    const double ratio = static_cast<double>(outputRate) / static_cast<double>(inputRate);
    return 0.94 * (ratio < 1.0 ? ratio : 1.0);
}

}  // namespace ivanna::audio
