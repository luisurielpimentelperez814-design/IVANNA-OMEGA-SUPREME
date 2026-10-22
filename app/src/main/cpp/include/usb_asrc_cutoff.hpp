#pragma once

// Frequency normalization for UsbIngressAsrc's sinc(x)=sin(pi*x)/(pi*x).
// This sinc convention expresses cutoff in cycles/sample; Nyquist is 0.5.
namespace ivanna::audio {

constexpr double usbAsrcCutoffCyclesPerSample(int inputRate, int outputRate) noexcept {
    if (inputRate <= 0 || outputRate <= 0) return 0.0;
    const double ratio = static_cast<double>(outputRate) / static_cast<double>(inputRate);
    return 0.47 * (ratio < 1.0 ? ratio : 1.0);
}

}  // namespace ivanna::audio
