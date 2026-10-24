#pragma once

// Converts PAL's paired pal_mmap_position into IVANNA's monotonic 64-bit
// frame sample. This class deliberately does not include private vendor headers;
// the target HAL supplies a tiny callback that calls
// pal_stream_get_mmap_position() on its OWN active pal_stream_handle_t.
#include "audio_hardware_clock.hpp"

#include <cstdint>
#include <limits>

namespace ivanna::audio {

class PalMmapPositionClockQuery {
public:
    // Must perform exactly one PAL query and return both fields from that result:
    //   pal_stream_get_mmap_position(stream, &position)
    //   frames = position.position_frames
    //   timeNs = position.time_nanoseconds
    // Return false on any PAL error (including -ENOSYS for non-MMAP streams).
    using QueryPosition = bool (*)(void* context,
                                   int32_t* frames,
                                   int64_t* timeNs) noexcept;

    PalMmapPositionClockQuery(QueryPosition query,
                              void* queryContext,
                              uint32_t nominalRateHz) noexcept
        : query_(query), queryContext_(queryContext), nominalRateHz_(nominalRateHz) {}

    PalMmapPositionClockQuery(const PalMmapPositionClockQuery&) = delete;
    PalMmapPositionClockQuery& operator=(const PalMmapPositionClockQuery&) = delete;

    // Suitable as QualcommPalClockIntegration::QueryPairedSample.
    static bool queryThunk(void* context, HardwareClockSample* out) noexcept {
        auto* self = static_cast<PalMmapPositionClockQuery*>(context);
        return self && self->query(out);
    }

    bool query(HardwareClockSample* out) noexcept {
        if (!out || !query_ || nominalRateHz_ < 8000 ||
            nominalRateHz_ > 768000) {
            reset();
            return false;
        }

        int32_t signedFrames = 0;
        int64_t timeNs = 0;
        if (!query_(queryContext_, &signedFrames, &timeNs) || timeNs <= 0) {
            reset();
            return false;
        }

        // PAL documents position_frames as a 32-bit counter. Interpret its bit
        // pattern as unsigned, then extend a genuine wrap. A stop/restart or
        // discontinuity must call reset(); an implausible jump fails closed.
        const uint32_t raw = static_cast<uint32_t>(signedFrames);
        if (!initialized_) {
            initialized_ = true;
            lastRawFrames_ = raw;
            extendedFrames_ = raw;
            lastTimestampNs_ = static_cast<uint64_t>(timeNs);
            *out = {extendedFrames_, lastTimestampNs_, nominalRateHz_};
            return true;
        }

        const uint64_t timestamp = static_cast<uint64_t>(timeNs);
        if (timestamp <= lastTimestampNs_) {
            reset();
            return false;
        }

        const uint32_t delta = static_cast<uint32_t>(raw - lastRawFrames_);
        // At the configured 10–20 Hz polling cadence, >2 s worth of frames is
        // a reset, stale sample, or wrong stream—not a plausible single delta.
        if (delta > static_cast<uint64_t>(nominalRateHz_) * 2ull ||
            extendedFrames_ > std::numeric_limits<uint64_t>::max() - delta) {
            reset();
            return false;
        }

        extendedFrames_ += delta;
        lastRawFrames_ = raw;
        lastTimestampNs_ = timestamp;
        *out = {extendedFrames_, timestamp, nominalRateHz_};
        return true;
    }

    void reset() noexcept {
        initialized_ = false;
        lastRawFrames_ = 0;
        extendedFrames_ = 0;
        lastTimestampNs_ = 0;
    }

private:
    QueryPosition query_ = nullptr;
    void* queryContext_ = nullptr;
    uint32_t nominalRateHz_ = 0;
    bool initialized_ = false;
    uint32_t lastRawFrames_ = 0;
    uint64_t extendedFrames_ = 0;
    uint64_t lastTimestampNs_ = 0;
};

} // namespace ivanna::audio
