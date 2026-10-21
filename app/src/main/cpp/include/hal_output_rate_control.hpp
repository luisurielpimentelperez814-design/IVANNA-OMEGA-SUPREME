#pragma once
// HAL-side closed-loop adapter. The owning output stream supplies real paired
// presentation samples and wires applyRatio() to its EXISTING variable-ratio ASRC.
#include "audio_hardware_clock.hpp"
#include "audio_hardware_clock_rate_servo.hpp"

namespace ivanna::audio {
class HalOutputRateControl {
public:
    using ApplyRatio = bool (*)(void* context, double inputFramesPerOutputFrame) noexcept;

    explicit HalOutputRateControl(ApplyRatio apply, void* context) noexcept
        : apply_(apply), context_(context) {}

    // Call from the HAL control cadence, not from the hard real-time write path.
    // Returns whether the requested ratio was accepted by the ASRC.
    // Before lock, unity is commanded; if the ASRC rejects unity, ratio state is unknown.
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
        ratioKnown_ = true;
        enabled_ = true;
        return true;
    }

    void reset() noexcept {
        tracker_.reset();
        servo_.reset();
        (void)applyUnity();
    }

    bool enabled() const noexcept { return enabled_; }
    // Only meaningful when hasKnownAppliedRatio() is true. This is the last
    // ratio accepted by the ASRC callback, not a measurement of hardware behavior.
    double lastAppliedRatio() const noexcept { return lastAppliedRatio_; }
    bool hasKnownAppliedRatio() const noexcept { return ratioKnown_; }

private:
    bool applyUnity() noexcept {
        enabled_ = false;
        if (!apply_ || !apply_(context_, 1.0)) {
            ratioKnown_ = false;
            return false;
        }
        lastAppliedRatio_ = 1.0;
        ratioKnown_ = true;
        return true;
    }

    HardwareClockTracker tracker_{};
    HardwareClockRateServo servo_{};
    ApplyRatio apply_ = nullptr;
    void* context_ = nullptr;
    double lastAppliedRatio_ = 1.0;
    bool ratioKnown_ = false;
    bool enabled_ = false;
};
} // namespace ivanna::audio
