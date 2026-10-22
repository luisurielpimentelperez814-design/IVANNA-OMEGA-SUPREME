#pragma once

// Occupancy-based correction for the USB ingress ASRC. This is deliberately
// separate from the FIR kernel so the exact controller used by production can
// be tested without USB hardware or JNI.
namespace ivanna::audio {

class UsbAsrcClockServo {
public:
    static constexpr double kMaxCorrection = 0.001; // ±1000 ppm
    static constexpr double kSmoothing = 0.002;

    void reset() noexcept { correction_ = 0.0; }

    double update(int ringFillFrames, int ringCapacityFrames) noexcept {
        if (ringCapacityFrames <= 0) {
            reset();
            return correction_;
        }

        const int fill = ringFillFrames < 0 ? 0 :
            (ringFillFrames > ringCapacityFrames ? ringCapacityFrames : ringFillFrames);
        const double target = static_cast<double>(ringCapacityFrames) * 0.5;
        const double error = (static_cast<double>(fill) - target) / target;
        const double desired = error * kMaxCorrection;
        correction_ += (desired - correction_) * kSmoothing;
        if (correction_ > kMaxCorrection) correction_ = kMaxCorrection;
        if (correction_ < -kMaxCorrection) correction_ = -kMaxCorrection;
        return correction_;
    }

    double correction() const noexcept { return correction_; }

private:
    double correction_ = 0.0;
};

} // namespace ivanna::audio
