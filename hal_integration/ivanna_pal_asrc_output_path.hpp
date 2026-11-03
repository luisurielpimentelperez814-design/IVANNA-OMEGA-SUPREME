#pragma once

// HAL-side PAL -> hardware-clock servo -> variable-ratio ASRC output stage.
// Instantiate this beside the long-lived owner of the active PAL output stream.
// Do not instantiate it in libomega_effect.so: AudioFlinger insert effects
// cannot change the frame count submitted to the physical PCM device.

#include "PalAsrcBridge.h"
#include "ivanna_qualcomm_pal_clock_integration.hpp"

#include <cmath>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace ivanna::audio {

class PalAsrcOutputPath {
public:
    using QueryPairedSample = QualcommPalClockIntegration::QueryPairedSample;

    PalAsrcOutputPath(QueryPairedSample query, void* queryContext) noexcept
        : query_(query),
          queryContext_(queryContext),
          clock_(&queryThunk, this, &applyRatioThunk, this) {}

    ~PalAsrcOutputPath() { stop(); }

    PalAsrcOutputPath(const PalAsrcOutputPath&) = delete;
    PalAsrcOutputPath& operator=(const PalAsrcOutputPath&) = delete;

    // Configure and allocate on the HAL stream setup/control thread, before
    // the render/write path starts. The HAL must size its output FIFO/period
    // for maxOutputFrames() and honour both framesConsumed and framesProduced.
    bool configure(const ::omega::AsrcConfig& config) {
        std::lock_guard<std::mutex> lock(controlMutex_);
        bridge_.setEnabled(false);
        configured_.store(false, std::memory_order_release);
        configuredOutputRateHz_.store(0, std::memory_order_relaxed);
        // A new PAL stream must never inherit lock or anchors from its predecessor.
        clock_.reset();
        bridge_.setManualRatio(1.0);
        if (!bridge_.configure(config)) return false;
        // The external PAL servo is the sole drift controller. Never estimate
        // sink drift from the cadence of this ASRC's own process() callbacks.
        bridge_.setDriftTracking(false);
        bridge_.setManualRatio(1.0);
        configuredOutputRateHz_.store(config.outputRateHz, std::memory_order_relaxed);
        bridge_.setEnabled(true);
        configured_.store(true, std::memory_order_release);
        return true;
    }

    // Optional telemetry for the existing AudioFlinger HardwareClockReader.
    // This does not make the insert effect an ASRC actuator.
    bool openReporter(const char* path = kHardwareClockSnapshotPath) noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        return clock_.openReporter(path);
    }

    // Call on a serialized HAL control task at ~10-20 Hz, never in the
    // hard-real-time write/render callback. Invalid/stale samples reset ratio
    // to unity; a new route must acquire lock again.
    bool controlTick() noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        if (!configured_.load(std::memory_order_acquire)) return false;
        return clock_.controlTick();
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        resetLocked();
    }

    void stop() noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        resetLocked();
        bridge_.setEnabled(false);
        configured_.store(false, std::memory_order_release);
        configuredOutputRateHz_.store(0, std::memory_order_relaxed);
    }

    bool correctionEnabled() const noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        return clock_.correctionEnabled();
    }
    bool hasKnownAppliedRatio() const noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        return clock_.hasKnownAppliedRatio();
    }
    double lastAppliedRatio() const noexcept {
        std::lock_guard<std::mutex> lock(controlMutex_);
        return clock_.lastAppliedRatio();
    }

    uint32_t maxOutputFrames(uint32_t inputFrames) const noexcept {
        return bridge_.maxOutputFrames(inputFrames);
    }
    uint32_t latencyFrames() const noexcept { return bridge_.latencyFrames(); }
    ::omega::PalAsrcBridge::Diagnostics diagnostics() const noexcept {
        return bridge_.getDiagnostics();
    }

    // Invoke on the HAL's actual PCM output path before its write operation.
    // Variable output length is intentional; the caller owns buffering and
    // must not silently discard unconsumed input or produced frames.
    ::omega::PalAsrcBridge::ProcessResult processF32(
            const float* input, uint32_t inputFrames,
            float* output, uint32_t outputCapacity) noexcept {
        return bridge_.processF32(input, inputFrames, output, outputCapacity);
    }

    ::omega::PalAsrcBridge::ProcessResult processS16(
            const int16_t* input, uint32_t inputFrames,
            int16_t* output, uint32_t outputCapacity) noexcept {
        return bridge_.processS16(input, inputFrames, output, outputCapacity);
    }

    ::omega::PalAsrcBridge::ProcessResult processS32(
            const int32_t* input, uint32_t inputFrames,
            int32_t* output, uint32_t outputCapacity) noexcept {
        return bridge_.processS32(input, inputFrames, output, outputCapacity);
    }

private:
    static bool queryThunk(void* context, HardwareClockSample* sample) noexcept {
        auto* self = static_cast<PalAsrcOutputPath*>(context);
        if (!self || !self->query_ || !sample ||
            !self->query_(self->queryContext_, sample)) return false;
        // A PAL clock measured against a different nominal stream rate cannot
        // control this resampler; require the stream to be reconfigured first.
        return sample->nominalSampleRateHz ==
               self->configuredOutputRateHz_.load(std::memory_order_acquire);
    }

    static bool applyRatioThunk(void* context, double inputFramesPerOutputFrame) noexcept {
        auto* self = static_cast<PalAsrcOutputPath*>(context);
        if (!self || !std::isfinite(inputFramesPerOutputFrame) ||
            inputFramesPerOutputFrame < 0.9994 ||
            inputFramesPerOutputFrame > 1.0006) return false;
        self->bridge_.setManualRatio(inputFramesPerOutputFrame);
        return true;
    }

    void resetLocked() noexcept {
        clock_.reset();
        bridge_.setManualRatio(1.0);
        bridge_.reset();
    }

    ::omega::PalAsrcBridge bridge_{};
    QueryPairedSample query_ = nullptr;
    void* queryContext_ = nullptr;
    QualcommPalClockIntegration clock_;
    // Serializes lifecycle/control operations; never acquired by the audio callback.
    mutable std::mutex controlMutex_;
    std::atomic<uint32_t> configuredOutputRateHz_{0};
    std::atomic<bool> configured_{false};
};

} // namespace ivanna::audio
