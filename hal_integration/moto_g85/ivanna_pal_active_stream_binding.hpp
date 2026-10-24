#pragma once

// Motorola/Qualcomm target binding for IVANNA's report-only PAL clock bridge.
// Include only from a matching vendor HAL build that provides PalApi.h.
// This publishes telemetry from the stream handle the HAL already owns. It
// deliberately does not open PCM devices, write mixer controls, or command ASRC.

#include <PalApi.h>
#include "ivanna_pal_active_stream_clock_publisher.hpp"

#include <cstdint>

namespace ivanna::audio::moto_g85 {

class ActivePalStreamClockBinding {
public:
    ActivePalStreamClockBinding(pal_stream_handle_t* activeStream,
                                uint32_t nominalRateHz) noexcept
        : context_{activeStream},
          publisher_(&queryPosition, &context_, nominalRateHz) {}

    ActivePalStreamClockBinding(const ActivePalStreamClockBinding&) = delete;
    ActivePalStreamClockBinding& operator=(const ActivePalStreamClockBinding&) = delete;

    // Call after the real output stream has opened and its rate is known.
    // The HAL's SELinux policy must allow this process to create/map the
    // configured IVHC snapshot path; failure leaves telemetry unavailable.
    bool start(const char* snapshotPath = kHardwareClockSnapshotPath) noexcept {
        return publisher_.start(snapshotPath);
    }

    // Call from the output stream's serialized, non-real-time control task
    // at 10–20 Hz. Never call from the audio render/write callback.
    bool poll() noexcept {
        if (!context_.stream) return false;
        return publisher_.poll();
    }

    // Must be called on standby, route changes, stream reopen, and teardown.
    void reset() noexcept { publisher_.reset(); }

    // Use only while the owning HAL stream is serialized. Rebind after the
    // vendor HAL replaces its PAL handle; the previous query state is discarded.
    void rebind(pal_stream_handle_t* activeStream) noexcept {
        publisher_.reset();
        context_.stream = activeStream;
    }

    void stop() noexcept {
        publisher_.stop();
        context_.stream = nullptr;
    }

private:
    struct Context {
        pal_stream_handle_t* stream;
    };

    static bool queryPosition(void* opaque, int32_t* frames,
                              int64_t* timeNs) noexcept {
        auto* context = static_cast<Context*>(opaque);
        if (!context || !context->stream || !frames || !timeNs) return false;

        pal_mmap_position position{};
        const int status =
            pal_stream_get_mmap_position(context->stream, &position);
        if (status != 0 || position.time_nanoseconds <= 0) return false;

        *frames = static_cast<int32_t>(position.position_frames);
        *timeNs = static_cast<int64_t>(position.time_nanoseconds);
        return true;
    }

    // Member order matters: publisher_ stores a pointer to context_.
    Context context_;
    PalMmapActiveStreamClockPublisher publisher_;
};

} // namespace ivanna::audio::moto_g85
