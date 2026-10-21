#pragma once

#include "audio_hardware_clock_bridge.hpp"

#include <cstdint>

namespace ivanna::audio {

// Thin adapter for a real Android audio HAL call site. Invoke this only after
// the HAL's own stream->get_presentation_position() (or equivalent driver API)
// succeeds and returns a frame counter plus its associated CLOCK_MONOTONIC
// timestamp. The timestamp must come from that API; never pass callback entry
// time or a timestamp sampled independently by this helper.
inline bool publishHalPresentationPosition(
        HardwareClockPublisher& publisher,
        uint64_t presentedFrames,
        int64_t timestampSeconds,
        int64_t timestampNanoseconds,
        uint32_t nominalSampleRateHz) noexcept {
    if (timestampSeconds < 0 || timestampNanoseconds < 0 ||
        timestampNanoseconds >= 1000000000ll) return false;
    const uint64_t seconds = static_cast<uint64_t>(timestampSeconds);
    if (seconds > (UINT64_MAX - static_cast<uint64_t>(timestampNanoseconds)) /
                  1000000000ull) return false;
    const uint64_t timestampNs = seconds * 1000000000ull +
                                 static_cast<uint64_t>(timestampNanoseconds);
    return publisher.publish({presentedFrames, timestampNs, nominalSampleRateHz});
}

} // namespace ivanna::audio
