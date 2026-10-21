#pragma once

// Concrete adapter for Android's legacy C audio HAL API.
// Include this only from a vendor HAL build that provides <hardware/audio.h>.
// AIDL/HIDL/PAL implementations should use ivanna_hal_output_stream.hpp and
// map their own presentation-position method to the same HardwareClockSample.
#include <hardware/audio.h>
#include <time.h>

#include <cstdint>

#include "ivanna_hal_output_stream.hpp"

namespace ivanna::audio {

class LegacyAudioHalClockAdapter {
public:
    LegacyAudioHalClockAdapter(audio_stream_out* stream,
                               uint32_t nominalSampleRateHz,
                               HalOutputRateControl::ApplyRatio applyRatio,
                               void* asrcContext) noexcept
        : stream_(stream),
          nominalSampleRateHz_(nominalSampleRateHz),
          control_(&queryPresentationPosition, this, applyRatio, asrcContext) {}

    LegacyAudioHalClockAdapter(const LegacyAudioHalClockAdapter&) = delete;
    LegacyAudioHalClockAdapter& operator=(const LegacyAudioHalClockAdapter&) = delete;

    // Invoke from the HAL's serialized low-rate control task (10–20 Hz),
    // not from write()/render(). The caller must stop/control this adapter
    // before stream close or route teardown.
    bool controlTick() noexcept { return control_.controlTick(); }

    // Invoke after standby/reopen, route changes, or stream discontinuity.
    void reset() noexcept { control_.reset(); }

    bool correctionEnabled() const noexcept { return control_.correctionEnabled(); }
    bool hasKnownAppliedRatio() const noexcept {
        return control_.hasKnownAppliedRatio();
    }
    double lastAppliedRatio() const noexcept { return control_.lastAppliedRatio(); }

private:
    static bool queryPresentationPosition(
        void* context, HardwareClockSample* out) noexcept {
        auto* self = static_cast<LegacyAudioHalClockAdapter*>(context);
        if (!self || !self->stream_ || !out ||
            !self->stream_->get_presentation_position ||
            self->nominalSampleRateHz_ < 8000 ||
            self->nominalSampleRateHz_ > 768000) {
            return false;
        }

        uint64_t frames = 0;
        timespec timestamp{};
        // Android's legacy HAL returns a paired presentation frame count and
        // driver timestamp. Do not replace this with callback-entry time.
        if (self->stream_->get_presentation_position(
                self->stream_, &frames, &timestamp) != 0) {
            return false;
        }
        if (timestamp.tv_sec < 0 || timestamp.tv_nsec < 0 ||
            timestamp.tv_nsec >= 1000000000L) {
            return false;
        }

        out->presentedFrames = frames;
        out->hardwareTimestampNs =
            static_cast<uint64_t>(timestamp.tv_sec) * 1000000000ull +
            static_cast<uint64_t>(timestamp.tv_nsec);
        out->nominalSampleRateHz = self->nominalSampleRateHz_;
        return out->hardwareTimestampNs != 0;
    }

    audio_stream_out* stream_ = nullptr;
    uint32_t nominalSampleRateHz_ = 0;
    HalOutputStreamClockControl control_;
};

} // namespace ivanna::audio
