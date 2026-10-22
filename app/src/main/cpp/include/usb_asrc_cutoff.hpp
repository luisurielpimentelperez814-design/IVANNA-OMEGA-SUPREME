#pragma once

// Frequency normalization for UsbIngressAsrc's sinc(x)=sin(pi*x)/(pi*x).
// Cycles/sample uses Nyquist=0.5; the normalized sinc argument uses Nyquist=1.
namespace ivanna::audio {

constexpr double usbAsrcCutoffCyclesPerSample(int inputRate, int outputRate) noexcept {
    if (inputRate <= 0 || outputRate <= 0) return 0.0;
    const double ratio = static_cast<double>(outputRate) / static_cast<double>(inputRate);
    return 0.47 * (ratio < 1.0 ? ratio : 1.0);
}

constexpr double usbAsrcCutoffNormalized(int inputRate, int outputRate) noexcept {
    return 2.0 * usbAsrcCutoffCyclesPerSample(inputRate, outputRate);
}

}  // namespace ivanna::audio
