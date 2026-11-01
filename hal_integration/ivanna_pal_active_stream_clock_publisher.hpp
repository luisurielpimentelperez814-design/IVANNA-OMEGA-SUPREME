#pragma once

// Concrete report-only bridge for a PAL output stream that supports MMAP
// position queries. The HAL owner supplies a callback bound to its EXISTING
// active pal_stream_handle_t. This class never opens a second PCM device and
// never commands an ASRC.
#include "ivanna_pal_mmap_position_adapter.hpp"
#include "audio_hardware_clock_bridge.hpp"

namespace ivanna::audio {

class PalMmapActiveStreamClockPublisher {
public:
    using QueryPosition = PalMmapPositionClockQuery::QueryPosition;

    PalMmapActiveStreamClockPublisher(QueryPosition query,
                                      void* streamContext,
                                      uint32_t nominalRateHz,
                                      uint64_t streamId,
                                      uint64_t routeGeneration) noexcept
        : query_(query, streamContext, nominalRateHz, streamId, routeGeneration) {}

    PalMmapActiveStreamClockPublisher(const PalMmapActiveStreamClockPublisher&) = delete;
    PalMmapActiveStreamClockPublisher& operator=(const PalMmapActiveStreamClockPublisher&) = delete;

    // Call during stream setup / on a non-real-time control thread.
    bool start(const char* path = kHardwareClockSnapshotPath) noexcept {
        return publisher_.open(path);
    }

    // Call at a serialized low rate (about 10–20 Hz), not from the render/write
    // callback. A successful publish contains frames and CLOCK_MONOTONIC ns
    // returned together by one PAL query. A failure publishes nothing.
    bool poll() noexcept {
        HardwareClockSample sample{};
        if (!query_.query(&sample)) {
            publisher_.invalidate();
            return false;
        }
        if (!publisher_.publish(sample)) {
            publisher_.invalidate();
            return false;
        }
        return true;
    }

    // Required on standby, stream close/reopen, route change, or discontinuity.
    void reset() noexcept { query_.reset(); publisher_.invalidate(); }

    void rebind(uint32_t nominalRateHz, uint64_t streamId,
                uint64_t routeGeneration) noexcept {
        reset();
        query_.rebind(nominalRateHz, streamId, routeGeneration);
    }

    void stop() noexcept {
        query_.reset();
        publisher_.close();
    }

private:
    PalMmapPositionClockQuery query_;
    HardwareClockPublisher publisher_;
};

} // namespace ivanna::audio
