#pragma once

// Source-level insertion stage for the Moto G85 primary HAL.
//
// Place one instance in the long-lived owner of EACH physical PAL output
// stream. Call processAndWrite() on the existing PCM buffer immediately
// before the owner's existing pal_stream_write() call.
//
// This adapter deliberately does not declare or reinterpret Qualcomm's
// private pal_buffer ABI. The target HAL supplies WriteBytes; that callback
// must construct pal_buffer using the exact matching vendor PalApi.h and call
// pal_stream_write() on the SAME stream handle.
//
// No allocations, locks, or logging occur in processAndWrite(). Configure and
// allocate on the stream setup/control thread. If a block is only partly
// consumed, the caller MUST retain and resubmit the unconsumed input frames.
// PAL may also accept a partial output write: this stage retains the unwritten
// output bytes and drains them before accepting another input block.

#include "ivanna_pal_asrc_output_path.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace ivanna::audio::moto_g85 {

class PalAsrcWriteStage {
public:
    using QueryPairedSample = PalAsrcOutputPath::QueryPairedSample;

    // PAL pal_stream_write returns ssize_t: positive bytes written, zero for
    // no progress, or a negative error. int64_t preserves that contract on
    // the supported 64-bit Android target without importing private headers.
    using WriteBytes = int64_t (*)(void* context, const void* data,
                                   size_t byteCount) noexcept;

    struct Result {
        uint32_t framesConsumed = 0;
        uint32_t framesProduced = 0;
        int64_t palBytesWrittenOrError = 0;
        size_t bytesPending = 0;
        bool processed = false;
        bool writeAttempted = false;
        bool writeComplete = false;
        bool writeContractViolation = false;
        bool partialFrameWrite = false;
    };

    PalAsrcWriteStage(QueryPairedSample query, void* queryContext,
                      WriteBytes write, void* writeContext) noexcept
        : path_(query, queryContext), write_(write), writeContext_(writeContext) {}

    PalAsrcWriteStage(const PalAsrcWriteStage&) = delete;
    PalAsrcWriteStage& operator=(const PalAsrcWriteStage&) = delete;

    // Call once on the serialized stream-setup thread, before playback.
    // maxInputFrames must be >= the largest input block this HAL will submit.
    bool configure(const ::omega::AsrcConfig& config,
                   uint32_t maxInputFrames) {
        stop();
        if (!write_ || maxInputFrames == 0 || config.channels == 0 ||
            config.channels > 8) return false;

        if (!path_.configure(config)) return false;

        const uint32_t maxOut = path_.maxOutputFrames(maxInputFrames);
        if (maxOut == 0) {
            path_.stop();
            return false;
        }

        const size_t sampleBytes = sizeof(std::max_align_t);
        const size_t bytesPerOutput =
            static_cast<size_t>(maxOut) * config.channels * sizeof(float);
        if (bytesPerOutput > std::numeric_limits<size_t>::max() -
                                 (sampleBytes - 1)) {
            path_.stop();
            return false;
        }

        // max_align_t storage safely accommodates float, int16_t and int32_t.
        const size_t words = (bytesPerOutput + sampleBytes - 1) / sampleBytes;
        try {
            scratch_.assign(words, std::max_align_t{});
        } catch (...) {
            scratch_.clear();
            path_.stop();
            return false;
        }

        channels_ = config.channels;
        maxInputFrames_ = maxInputFrames;
        scratchBytes_ = words * sampleBytes;
        configured_ = true;
        pendingBytes_ = 0;
        pendingOffset_ = 0;
        bytesPerFrame_ = 0;
        poisoned_ = false;
        return true;
    }

    bool controlTick() noexcept {
        return configured_ && path_.controlTick();
    }

    void reset() noexcept {
        path_.reset();
        // A route/clock discontinuity invalidates any queued converted PCM.
        pendingBytes_ = 0;
        pendingOffset_ = 0;
        bytesPerFrame_ = 0;
        poisoned_ = false;
    }

    void stop() noexcept {
        path_.stop();
        scratch_.clear();
        channels_ = 0;
        maxInputFrames_ = 0;
        scratchBytes_ = 0;
        pendingBytes_ = 0;
        pendingOffset_ = 0;
        bytesPerFrame_ = 0;
        poisoned_ = false;
        configured_ = false;
    }

    bool correctionEnabled() const noexcept {
        return configured_ && path_.correctionEnabled();
    }
    bool hasKnownAppliedRatio() const noexcept {
        return configured_ && path_.hasKnownAppliedRatio();
    }
    double lastAppliedRatio() const noexcept {
        return configured_ ? path_.lastAppliedRatio() : 1.0;
    }
    ::omega::PalAsrcBridge::Diagnostics diagnostics() const noexcept {
        return path_.diagnostics();
    }

    Result processAndWriteF32(const float* input, uint32_t inputFrames) noexcept {
        return processAndWrite(input, inputFrames);
    }
    Result processAndWriteS16(const int16_t* input, uint32_t inputFrames) noexcept {
        return processAndWrite(input, inputFrames);
    }
    Result processAndWriteS32(const int32_t* input, uint32_t inputFrames) noexcept {
        return processAndWrite(input, inputFrames);
    }

private:
    bool flushPending(Result& result) noexcept {
        if (pendingBytes_ == 0) {
            result.writeComplete = true;
            return true;
        }
        if (poisoned_ || !write_ || bytesPerFrame_ == 0) {
            result.bytesPending = pendingBytes_ - pendingOffset_;
            return false;
        }

        const size_t remaining = pendingBytes_ - pendingOffset_;
        result.writeAttempted = true;
        const auto* bytes =
            reinterpret_cast<const uint8_t*>(scratch_.data()) + pendingOffset_;
        const int64_t written = write_(writeContext_, bytes, remaining);
        result.palBytesWrittenOrError = written;

        if (written < 0) {
            result.bytesPending = remaining;
            return false; // Preserve pending PCM; caller handles stream error.
        }
        if (static_cast<uint64_t>(written) > remaining) {
            poisoned_ = true;
            result.writeContractViolation = true;
            result.bytesPending = remaining;
            return false;
        }
        if (written == 0) {
            result.bytesPending = remaining;
            return false; // No progress: retry only on the next caller entry.
        }

        const size_t accepted = static_cast<size_t>(written);
        if ((accepted % bytesPerFrame_) != 0) {
            // PAL accepted a fraction of a PCM frame. Continuing would submit
            // a misaligned buffer, so fail closed until reset/reconfigure.
            pendingOffset_ += accepted;
            poisoned_ = true;
            result.partialFrameWrite = true;
            result.bytesPending = pendingBytes_ - pendingOffset_;
            return false;
        }

        pendingOffset_ += accepted;
        if (pendingOffset_ == pendingBytes_) {
            pendingBytes_ = 0;
            pendingOffset_ = 0;
            result.bytesPending = 0;
            result.writeComplete = true;
            return true;
        }

        result.bytesPending = pendingBytes_ - pendingOffset_;
        return false;
    }

    template <typename Sample>
    Result processAndWrite(const Sample* input, uint32_t inputFrames) noexcept {
        Result result{};
        if (!configured_ || !input || inputFrames == 0 ||
            inputFrames > maxInputFrames_ || !write_ || poisoned_) return result;

        // Never accept new input while a previous converted block is pending.
        // If it drains now, this call may proceed with the caller's current
        // (still-unconsumed) input block.
        if (pendingBytes_ != 0 && !flushPending(result)) return result;

        const uint32_t capacityFrames = path_.maxOutputFrames(inputFrames);
        const size_t requiredBytes =
            static_cast<size_t>(capacityFrames) * channels_ * sizeof(Sample);
        if (requiredBytes > scratchBytes_) return result;

        auto* output = reinterpret_cast<Sample*>(scratch_.data());
        const auto converted = process(input, inputFrames, output, capacityFrames);
        result.framesConsumed = converted.framesConsumed;
        result.framesProduced = converted.framesProduced;
        result.processed = true;

        if (converted.framesProduced == 0) {
            result.writeComplete = true;
            return result;
        }

        pendingBytes_ =
            static_cast<size_t>(converted.framesProduced) * channels_ * sizeof(Sample);
        pendingOffset_ = 0;
        bytesPerFrame_ = static_cast<size_t>(channels_) * sizeof(Sample);
        (void)flushPending(result);
        return result;
    }

    ::omega::PalAsrcBridge::ProcessResult process(
            const float* in, uint32_t frames, float* out, uint32_t cap) noexcept {
        return path_.processF32(in, frames, out, cap);
    }
    ::omega::PalAsrcBridge::ProcessResult process(
            const int16_t* in, uint32_t frames, int16_t* out, uint32_t cap) noexcept {
        return path_.processS16(in, frames, out, cap);
    }
    ::omega::PalAsrcBridge::ProcessResult process(
            const int32_t* in, uint32_t frames, int32_t* out, uint32_t cap) noexcept {
        return path_.processS32(in, frames, out, cap);
    }

    PalAsrcOutputPath path_;
    WriteBytes write_ = nullptr;
    void* writeContext_ = nullptr;
    std::vector<std::max_align_t> scratch_;
    uint32_t channels_ = 0;
    uint32_t maxInputFrames_ = 0;
    size_t scratchBytes_ = 0;
    size_t pendingBytes_ = 0;
    size_t pendingOffset_ = 0;
    size_t bytesPerFrame_ = 0;
    bool poisoned_ = false;
    bool configured_ = false;
};

} // namespace ivanna::audio::moto_g85
