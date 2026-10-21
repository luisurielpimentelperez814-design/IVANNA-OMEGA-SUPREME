#pragma once
// HAL-side closed-loop adapter. The owning output stream supplies real paired
// presentation samples and wires applyRatio() to its EXISTING variable-ratio ASRC.
#include "audio_hardware_clock.hpp"
#include "audio_hardware_clock_rate_servo.hpp"
namespace ivanna::audio {
class HalOutputRateControl {
public:
    using ApplyRatio = bool (*)(void* context, double inputFramesPerOutputFrame) noexcept;
    explicit HalOutputRateControl(ApplyRatio apply, void* context) noexcept : apply_(apply), context_(context) {}
    // Call from the HAL control cadence, not from the hard real-time write path.
    // Returns whether the ratio was accepted; pre-lock safely commands unity.
    bool update(const HardwareClockSample& sample) noexcept {
        const auto status = tracker_.observe(sample);
        const auto command = servo_.observe(status);
        if (!command.enabled) return applyUnity();
        if (!apply_ || !apply_(context_, command.inputFramesPerOutputFrame)) {
            servo_.reset();
            (void)applyUnity();
            return false;
        }
        lastAppliedRatio_ = command.inputFramesPerOutputFrame;
        enabled_ = true;
        return true;
    }
    void reset() noexcept { tracker_.reset(); servo_.reset(); (void)applyUnity(); }
    bool enabled() const noexcept { return enabled_; }
    double lastAppliedRatio() const noexcept { return lastAppliedRatio_; }
private:
    bool applyUnity() noexcept {
        enabled_ = false; lastAppliedRatio_ = 1.0;
        return apply_ && apply_(context_, 1.0);
    }
    HardwareClockTracker tracker_{};
    HardwareClockRateServo servo_{};
    ApplyRatio apply_ = nullptr;
    void* context_ = nullptr;
    double lastAppliedRatio_ = 1.0;
    bool enabled_ = false;
};
} // namespace ivanna::audio
