#pragma once

#include <cmath>
#include <cstdint>

namespace ivanna::audio {

// Measures the callback's observed frame cadence against CLOCK_MONOTONIC.
// This is a software callback clock monitor, NOT a DAC/hardware clock sensor.
// It must not be used as a hardware phase reference for the geometric PLL.
struct CallbackClockStatus {
    bool valid = false;
    uint64_t elapsedNs = 0;
    uint64_t frames = 0;
    double observedSampleRateHz = 0.0;
    double driftPpm = 0.0;
};

class CallbackClockMonitor {
public:
    CallbackClockStatus observe(uint64_t monotonicNs, uint64_t frameCount,
                                uint32_t nominalSampleRateHz) noexcept {
        if (monotonicNs == 0 || frameCount == 0 ||
            nominalSampleRateHz < 8000 || nominalSampleRateHz > 768000) {
            reset();
            return {};
        }
        if (!initialized_) {
            initialized_ = true;
            startNs_ = monotonicNs;
            lastNs_ = monotonicNs;
                windowFrames_ = frameCount;
            return {};
        }
        if (monotonicNs <= lastNs_ || monotonicNs < startNs_) {
            reset();
            return {};
        }

        lastNs_ = monotonicNs;
        windowFrames_ += frameCount;
        const uint64_t elapsed = monotonicNs - startNs_;
        // Ignore short windows: scheduling jitter dominates sub-100ms estimates.
        if (elapsed < kMinWindowNs) return {};

        const double rate = static_cast<double>(windowFrames_) * 1.0e9 /
                            static_cast<double>(elapsed);
        const double drift = (rate / static_cast<double>(nominalSampleRateHz) - 1.0) *
                             1.0e6;
        if (!std::isfinite(rate) || !std::isfinite(drift)) {
            reset();
            return {};
        }
        return {true, elapsed, windowFrames_, rate, drift};
    }

    void reset() noexcept {
        initialized_ = false;
        startNs_ = lastNs_ = windowFrames_ = 0;
    }

private:
    static constexpr uint64_t kMinWindowNs = 100000000ull;
    bool initialized_ = false;
    uint64_t startNs_ = 0;
    uint64_t lastNs_ = 0;
    uint64_t windowFrames_ = 0;
};

} // namespace ivanna::audio
