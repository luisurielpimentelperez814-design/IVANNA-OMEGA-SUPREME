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
// A failed PAL write is a stream error: do not replay already-consumed input.

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

    // Must submit exactly byteCount bytes to the existing PAL stream and
    // return the real pal_stream_write status. It must not open another stream.
    using WriteBytes = int32_t (*)(void* context, const void* data,
                                   size_t byteCount) noexcept;

    struct Result {
        uint32_t framesConsumed = 0;
        uint32_t framesProduced = 0;
        int32_t palStatus = 0;
        bool processed = false;
        bool writeAttempted = false;
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
                                 (sampleBytes - 1)) return false;

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
        return true;
    }

    bool controlTick() noexcept {
        return configured_ && path_.controlTick();
    }

    void reset() noexcept {
        path_.reset();
    }

    void stop() noexcept {
        path_.stop();
        scratch_.clear();
        channels_ = 0;
        maxInputFrames_ = 0;
        scratchBytes_ = 0;
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
    template <typename Sample>
    Result processAndWrite(const Sample* input, uint32_t inputFrames) noexcept {
        Result result{};
        if (!configured_ || !input || inputFrames == 0 ||
            inputFrames > maxInputFrames_ || !write_) return result;

        const uint32_t capacityFrames = path_.maxOutputFrames(inputFrames);
        const size_t requiredBytes =
            static_cast<size_t>(capacityFrames) * channels_ * sizeof(Sample);
        if (requiredBytes > scratchBytes_) return result;

        auto* output = reinterpret_cast<Sample*>(scratch_.data());
        const auto converted = process(input, inputFrames, output, capacityFrames);
        result.framesConsumed = converted.framesConsumed;
        result.framesProduced = converted.framesProduced;
        result.processed = true;

        if (converted.framesProduced == 0) return result;

        const size_t outputBytes =
            static_cast<size_t>(converted.framesProduced) * channels_ * sizeof(Sample);
        result.writeAttempted = true;
        result.palStatus = write_(writeContext_, output, outputBytes);
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
    bool configured_ = false;
};

} // namespace ivanna::audio::moto_g85
