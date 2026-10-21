#pragma once

// Reusable HAL-side control hook. The target HAL owns the stream, obtains the
// frame/timestamp pair from its real presentation-position API, and provides
// the callback that updates its existing variable-ratio ASRC.
#include "hal_output_rate_control.hpp"

namespace ivanna::audio {

class HalOutputStreamClockControl {
public:
    using QueryPresentationPosition =
        bool (*)(void* context, HardwareClockSample* sample) noexcept;
    using ApplyRatio = HalOutputRateControl::ApplyRatio;

    HalOutputStreamClockControl(QueryPresentationPosition query,
                                ApplyRatio applyRatio,
                                void* context) noexcept
        : query_(query), context_(context), rateControl_(applyRatio, context) {}

    HalOutputStreamClockControl(const HalOutputStreamClockControl&) = delete;
    HalOutputStreamClockControl& operator=(const HalOutputStreamClockControl&) = delete;

    // Call on a low-rate HAL control cadence (e.g. 10–20 Hz), never in the
    // audio write/render callback. Query must return a paired driver frame
    // position and CLOCK_MONOTONIC timestamp from one successful HAL query.
    bool controlTick() noexcept {
        if (!query_) {
            rateControl_.reset();
            return false;
        }
        HardwareClockSample sample{};
        if (!query_(context_, &sample)) {
            rateControl_.reset();
            return false;
        }
        return rateControl_.update(sample);
    }

    // Call on stream reopen, standby, route change, or timestamp discontinuity.
    void reset() noexcept { rateControl_.reset(); }

    bool correctionEnabled() const noexcept { return rateControl_.enabled(); }
    bool hasKnownAppliedRatio() const noexcept {
        return rateControl_.hasKnownAppliedRatio();
    }
    double lastAppliedRatio() const noexcept {
        return rateControl_.lastAppliedRatio();
    }

private:
    QueryPresentationPosition query_ = nullptr;
    void* context_ = nullptr;
    HalOutputRateControl rateControl_;
};

} // namespace ivanna::audio
