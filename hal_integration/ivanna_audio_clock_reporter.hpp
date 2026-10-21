#pragma once

// Add this header to the target vendor audio HAL build include paths.
// The caller must supply values returned together by the HAL/driver presentation
// position API. This helper does not query or fabricate a hardware timestamp.
#include "audio_hardware_clock_hal_adapter.hpp"
#include <time.h>

namespace ivanna::audio {

class HalOutputClockReporter {
public:
    HalOutputClockReporter() = default;
    HalOutputClockReporter(const HalOutputClockReporter&) = delete;
    HalOutputClockReporter& operator=(const HalOutputClockReporter&) = delete;

    // Call during HAL stream initialization, not from a hard/real-time callback.
    bool start() noexcept { return publisher_.open(); }

    // Call after a successful get_presentation_position() result. Frames and
    // timespec must be from the same successful driver query. It performs only
    // bounded atomic stores after the publisher has been opened.
    bool report(uint64_t frames, const timespec& timestamp,
                uint32_t nominalSampleRateHz) noexcept {
        return publishHalPresentationPosition(
            publisher_, frames, static_cast<int64_t>(timestamp.tv_sec),
            static_cast<int64_t>(timestamp.tv_nsec), nominalSampleRateHz);
    }

    void stop() noexcept { publisher_.close(); }

private:
    HardwareClockPublisher publisher_;
};

} // namespace ivanna::audio
