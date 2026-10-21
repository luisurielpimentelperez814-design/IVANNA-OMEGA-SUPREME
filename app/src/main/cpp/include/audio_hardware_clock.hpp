#pragma once

#include <cmath>
#include <cstdint>

namespace ivanna::audio {

// Input must come from the actual output HAL/driver presentation-position API
// (for example get_presentation_position / AAudioStream_getTimestamp).
// Do NOT feed CLOCK_MONOTONIC sampled at the effect callback as a substitute.
struct HardwareClockSample {
    uint64_t presentedFrames = 0;
    uint64_t hardwareTimestampNs = 0;
    uint32_t nominalSampleRateHz = 0;
};

struct HardwareClockStatus {
    bool valid = false;
    uint64_t frameDelta = 0;
    uint64_t elapsedNs = 0;
    double measuredSampleRateHz = 0.0;
    double driftPpm = 0.0;
};

// Estimates the physical output clock from two driver-reported presentation
// positions. This class does not obtain timestamps itself: the HAL/driver must
// call observe() with its real hardware-correlated timestamp and frame counter.
// No allocation, locks, syscalls, or waiting; suitable for a preallocated caller.
class HardwareClockTracker {
public:
    HardwareClockStatus observe(const HardwareClockSample& sample) noexcept {
        if (sample.hardwareTimestampNs == 0 ||
            sample.nominalSampleRateHz < 8000 ||
            sample.nominalSampleRateHz > 768000) {
            reset();
            return {};
        }
        if (!initialized_) {
            previous_ = sample;
            initialized_ = true;
            return {};
        }

        if (sample.hardwareTimestampNs <= previous_.hardwareTimestampNs ||
            sample.presentedFrames <= previous_.presentedFrames ||
            sample.nominalSampleRateHz != previous_.nominalSampleRateHz) {
            reset();
            return {};
        }

        const uint64_t frames = sample.presentedFrames - previous_.presentedFrames;
        const uint64_t elapsed = sample.hardwareTimestampNs - previous_.hardwareTimestampNs;
        // Reject stale or discontinuous driver snapshots rather than producing
        // a bogus frequency estimate that could steer the PLL.
        previous_ = sample;
        if (elapsed < kMinIntervalNs || elapsed > kMaxIntervalNs ||
            frames > static_cast<uint64_t>(sample.nominalSampleRateHz) * 2ull) {
            return {};
        }

        const double rate = static_cast<double>(frames) * 1.0e9 /
                            static_cast<double>(elapsed);
        const double ppm = (rate / static_cast<double>(sample.nominalSampleRateHz) - 1.0)
                           * 1.0e6;
        if (!std::isfinite(rate) || !std::isfinite(ppm) ||
            rate < sample.nominalSampleRateHz * 0.5 ||
            rate > sample.nominalSampleRateHz * 1.5) {
            return {};
        }
        return {true, frames, elapsed, rate, ppm};
    }

    void reset() noexcept {
        initialized_ = false;
        previous_ = {};
    }

private:
    static constexpr uint64_t kMinIntervalNs = 1000000ull;       // 1 ms
    static constexpr uint64_t kMaxIntervalNs = 2000000000ull;    // 2 s
    bool initialized_ = false;
    HardwareClockSample previous_{};
};

} // namespace ivanna::audio
