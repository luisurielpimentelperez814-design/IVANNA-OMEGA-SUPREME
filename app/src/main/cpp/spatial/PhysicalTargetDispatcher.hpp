#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ivanna::spatial {

// Experimental DSP kernel: 16 virtual inputs -> five logical output buses.
// It does not open an Android audio device or claim that the device exposes five
// physical PCM channels. The caller must supply a calibrated, bounded matrix.
class PhysicalTargetDispatcher final {
public:
    static constexpr std::size_t kInputs = 16;
    static constexpr std::size_t kOutputs = 5;

    using Matrix = std::array<float, kInputs * kOutputs>;

    struct ThermalState {
        // Must come from a real sensor/driver or explicit user measurement.
        // NaN/Inf and implausible values are rejected, never silently estimated.
        float windingTemperatureC = 25.0f;
        bool temperatureValid = false;
    };

    PhysicalTargetDispatcher() noexcept;

    // Matrix is row-major by output: coeff[out * kInputs + input].
    // Rejects non-finite values and coefficients outside [-2, 2].
    bool setMatrix(const Matrix& matrix) noexcept;
    const Matrix& matrix() const noexcept { return matrix_; }

    // Processes interleaved input frames into five interleaved logical buses.
    // Caller owns both buffers; no allocations, locks, JNI calls, or syscalls.
    // Returns false and writes silence if pointers/counts/inputs are invalid.
    bool process(const float* input16Interleaved,
                 float* output5Interleaved,
                 std::size_t frames,
                 float outputGain = 1.0f) const noexcept;

    // Copper resistance ratio R(T)/R(20 C), for diagnostics only. This is NOT
    // a temperature estimator and does not derive temperature from nominal ohms.
    static bool copperResistanceRatio(float temperatureC, float* ratio) noexcept;

    // Optional conservative output derating from a verified temperature sensor.
    // Returns unity when no valid sensor is available; never boosts above 1.
    static float thermalDerating(const ThermalState& state) noexcept;

private:
    Matrix matrix_{};
};

} // namespace ivanna::spatial
