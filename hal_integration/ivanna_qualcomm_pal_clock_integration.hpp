#pragma once

// Qualcomm PAL integration seam for IVANNA's hardware-clock controller.
// This deliberately does not guess Qualcomm's private pal_session_time layout
// or a DSP parameter ID. The target HAL supplies the two device-specific calls:
//   1) a real, paired frame-position + hardware-correlated timestamp query;
//   2) a command to the variable-ratio ASRC already in the output path.
#include "ivanna_hal_output_stream.hpp"
#include "audio_hardware_clock_bridge.hpp"

#include <time.h>

namespace ivanna::audio {

class QualcommPalClockIntegration {
public:
    using QueryPairedSample =
        bool (*)(void* context, HardwareClockSample* sample) noexcept;
    using ApplyExistingAsrcRatio = HalOutputRateControl::ApplyRatio;

    QualcommPalClockIntegration(QueryPairedSample query,
                                void* queryContext,
                                ApplyExistingAsrcRatio applyRatio,
                                void* asrcContext) noexcept
        : query_(query),
          queryContext_(queryContext),
          control_(&queryAndPublish, this, applyRatio, asrcContext) {}

    QualcommPalClockIntegration(const QualcommPalClockIntegration&) = delete;
    QualcommPalClockIntegration& operator=(const QualcommPalClockIntegration&) = delete;

    // Call once during stream setup, off the audio render/write path.
    // Failure to open the optional IPC publisher does not disable local control.
    bool openReporter(
        const char* path = kHardwareClockSnapshotPath) noexcept {
        reporterOpen_ = publisher_.open(path);
        return reporterOpen_;
    }

    // Schedule from the stream's serialized control task at ~10–20 Hz.
    // Never invoke from the hard real-time write/render callback.
    bool controlTick() noexcept { return control_.controlTick(); }

    // Call on standby, reopen, route change, or timestamp discontinuity.
    void reset() noexcept {
        control_.reset();
        if (reporterOpen_) publisher_.invalidate();
    }

    bool correctionEnabled() const noexcept {
        return control_.correctionEnabled();
    }
    bool hasKnownAppliedRatio() const noexcept {
        return control_.hasKnownAppliedRatio();
    }
    double lastAppliedRatio() const noexcept {
        return control_.lastAppliedRatio();
    }
    bool reporterOpen() const noexcept { return reporterOpen_; }

private:
    static bool queryAndPublish(void* context,
                                HardwareClockSample* out) noexcept {
        auto* self = static_cast<QualcommPalClockIntegration*>(context);
        if (!self || !self->query_ || !out) return false;

        HardwareClockSample sample{};
        timespec now{};
        if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
            if (self->reporterOpen_) self->publisher_.invalidate();
            return false;
        }
        const uint64_t nowNs = static_cast<uint64_t>(now.tv_sec) * 1000000000ull +
                               static_cast<uint64_t>(now.tv_nsec);
        if (!self->query_(self->queryContext_, &sample) ||
            !isFreshMonotonicHardwareClockSample(sample, nowNs)) {
            if (self->reporterOpen_) self->publisher_.invalidate();
            return false;
        }

        // Reporting is best-effort; failure to publish must not fabricate or
        // replace the hardware sample used by the local controller.
        if (self->reporterOpen_ && !self->publisher_.publish(sample))
            self->publisher_.invalidate();
        *out = sample;
        return true;
    }

    QueryPairedSample query_ = nullptr;
    void* queryContext_ = nullptr;
    HardwareClockPublisher publisher_{};
    bool reporterOpen_ = false;
    HalOutputStreamClockControl control_;
};

} // namespace ivanna::audio
