#pragma once

// Diagnostic-only adapter for Qualcomm PAL's public pal_get_timestamp API.
// Public PAL defines session_time, absolute_time and timestamp as 64-bit
// microsecond values split into low/high 32-bit words. These values are NOT
// a paired presentation-frame count + CLOCK_MONOTONIC timestamp, so this
// observer intentionally does not feed HardwareClockTracker or the ASRC servo.
//
// Include from a vendor HAL target that already has PAL headers/linkage.
// The stream handle must remain owned and valid for the observer's lifetime.
#include <PalApi.h>

#include <cstdint>

namespace ivanna::audio {

struct QualcommPalTimestampSnapshot {
    uint64_t sessionTimeUs = 0;
    uint64_t absoluteTimeUs = 0;
    uint64_t processedTimestampUs = 0;
};

class QualcommPalTimestampObserver {
public:
    using GetTimestamp = int32_t (*)(pal_stream_handle_t*,
                                     struct pal_session_time*);

    explicit QualcommPalTimestampObserver(
        pal_stream_handle_t* stream,
        GetTimestamp getTimestamp = &pal_get_timestamp) noexcept
        : stream_(stream), getTimestamp_(getTimestamp) {}

    QualcommPalTimestampObserver(const QualcommPalTimestampObserver&) = delete;
    QualcommPalTimestampObserver& operator=(
        const QualcommPalTimestampObserver&) = delete;

    // Safe for a serialized, low-rate diagnostic task; do not call from the
    // real-time audio write/render callback. False means no usable snapshot.
    bool query(QualcommPalTimestampSnapshot* out) const noexcept {
        if (!stream_ || !getTimestamp_ || !out) return false;

        struct pal_session_time raw {};
        if (getTimestamp_(stream_, &raw) != 0) return false;

        QualcommPalTimestampSnapshot snapshot{};
        snapshot.sessionTimeUs = combine(raw.session_time);
        snapshot.absoluteTimeUs = combine(raw.absolute_time);
        snapshot.processedTimestampUs = combine(raw.timestamp);

        // Zero is treated as unavailable rather than a valid clock reading.
        if (snapshot.sessionTimeUs == 0 &&
            snapshot.absoluteTimeUs == 0 &&
            snapshot.processedTimestampUs == 0) {
            return false;
        }

        *out = snapshot;
        return true;
    }

private:
    static uint64_t combine(const struct pal_time_us& value) noexcept {
        return (static_cast<uint64_t>(value.value_msw) << 32) |
               static_cast<uint64_t>(value.value_lsw);
    }

    pal_stream_handle_t* stream_ = nullptr;
    GetTimestamp getTimestamp_ = nullptr;
};

} // namespace ivanna::audio
