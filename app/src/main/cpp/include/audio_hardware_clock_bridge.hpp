#pragma once

#include "audio_hardware_clock.hpp"

#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ivanna::audio {

// Cross-process contract between one authoritative output HAL/PCM owner and
// the AudioFlinger effect. Exactly one publisher may own this path at a time;
// concurrent output streams must be arbitrated by the HAL before publication.
// The producer MUST publish a hardware-correlated
// presentation timestamp and the matching presented-frame position.
// Never publish CLOCK_MONOTONIC sampled at effect callback entry.
inline constexpr const char* kHardwareClockSnapshotPath =
        "/data/vendor/ivanna_audio/clock_snapshot.v2";
inline constexpr uint32_t kHardwareClockSnapshotMagic = 0x32485649u; // "IVH2" — reject v1 readers
inline constexpr size_t kHardwareClockSnapshotBytes = 64;
inline constexpr uint32_t kHardwareClockSnapshotVersion = 2;
inline constexpr uint32_t kHardwareClockSnapshotActive = 1u;

// A shared snapshot can outlive the output route/process that published it.
// Consumers must reject future timestamps and stale snapshots before treating
// them as a live route. nowMonotonicNs must come from CLOCK_MONOTONIC.
inline bool isFreshMonotonicHardwareClockSample(
        const HardwareClockSample& sample,
        uint64_t nowMonotonicNs,
        uint64_t maxAgeNs = 1000000000ull) noexcept {
    if (sample.streamId == 0 || sample.routeGeneration == 0 ||
        sample.hardwareTimestampNs == 0 ||
        sample.hardwareTimestampNs > nowMonotonicNs ||
        sample.nominalSampleRateHz < 8000 ||
        sample.nominalSampleRateHz > 768000) return false;
    return nowMonotonicNs - sample.hardwareTimestampNs <= maxAgeNs;
}

struct alignas(8) HardwareClockSnapshot {
    std::atomic<uint32_t> magic{0};
    std::atomic<uint32_t> version{0};
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> flags{0};
    std::atomic<uint64_t> streamId{0};
    std::atomic<uint64_t> routeGeneration{0};
    std::atomic<uint64_t> presentedFrames{0};
    std::atomic<uint64_t> hardwareTimestampNs{0};
    std::atomic<uint32_t> nominalSampleRateHz{0};
    std::atomic<uint32_t> reserved{0};
};
static_assert(sizeof(HardwareClockSnapshot) == 56,
              "IVHC v2 shared-memory layout changed unexpectedly");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "hardware clock IPC requires lock-free 64-bit atomics");

// Construct/open from a HAL/PCM output owner during stream setup, then call
// publish() with the driver's real presentation-position snapshot. publish()
// is allocation-free, syscall-free, and bounded for use after open().
class HardwareClockPublisher {
public:
    HardwareClockPublisher() = default;
    HardwareClockPublisher(const HardwareClockPublisher&) = delete;
    HardwareClockPublisher& operator=(const HardwareClockPublisher&) = delete;
    ~HardwareClockPublisher() { close(); }

    bool open(const char* path = kHardwareClockSnapshotPath) noexcept {
        if (snapshot_) return true;
        const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0660);
        if (fd < 0) return false;
        struct stat st{};
        if (::fstat(fd, &st) < 0 ||
            (st.st_size != 0 &&
             static_cast<size_t>(st.st_size) != kHardwareClockSnapshotBytes)) {
            // Never truncate an unknown/legacy snapshot layout in place.
            ::close(fd);
            return false;
        }
        (void)::fchmod(fd, 0660);
        if (st.st_size == 0 && ::ftruncate(fd, kHardwareClockSnapshotBytes) < 0) {
            ::close(fd);
            return false;
        }
        void* memory = ::mmap(nullptr, kHardwareClockSnapshotBytes,
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (memory == MAP_FAILED) return false;
        snapshot_ = static_cast<HardwareClockSnapshot*>(memory);
        // Invalidate the old ABI before touching fields so v1 readers cannot
        // interpret the v2 layout while a publisher is being initialized.
        snapshot_->magic.store(0, std::memory_order_release);
        snapshot_->version.store(kHardwareClockSnapshotVersion, std::memory_order_relaxed);
        snapshot_->sequence.store(0, std::memory_order_relaxed);
        snapshot_->flags.store(0, std::memory_order_relaxed);
        snapshot_->streamId.store(0, std::memory_order_relaxed);
        snapshot_->routeGeneration.store(0, std::memory_order_relaxed);
        snapshot_->presentedFrames.store(0, std::memory_order_relaxed);
        snapshot_->hardwareTimestampNs.store(0, std::memory_order_relaxed);
        snapshot_->nominalSampleRateHz.store(0, std::memory_order_relaxed);
        snapshot_->magic.store(kHardwareClockSnapshotMagic, std::memory_order_release);
        return true;
    }

    bool publish(const HardwareClockSample& sample) noexcept {
        auto* s = snapshot_;
        if (!s || sample.streamId == 0 || sample.routeGeneration == 0 ||
            sample.hardwareTimestampNs == 0 ||
            sample.nominalSampleRateHz < 8000 ||
            sample.nominalSampleRateHz > 768000) return false;
        uint32_t seq = s->sequence.load(std::memory_order_relaxed);
        if (seq & 1u) ++seq;
        s->sequence.store(seq + 1u, std::memory_order_release); // write in progress
        s->streamId.store(sample.streamId, std::memory_order_relaxed);
        s->routeGeneration.store(sample.routeGeneration, std::memory_order_relaxed);
        s->presentedFrames.store(sample.presentedFrames, std::memory_order_relaxed);
        s->hardwareTimestampNs.store(sample.hardwareTimestampNs, std::memory_order_relaxed);
        s->nominalSampleRateHz.store(sample.nominalSampleRateHz, std::memory_order_relaxed);
        s->flags.store(kHardwareClockSnapshotActive, std::memory_order_relaxed);
        s->sequence.store(seq + 2u, std::memory_order_release);
        return true;
    }

    void invalidate() noexcept {
        auto* s = snapshot_;
        if (!s) return;
        uint32_t seq = s->sequence.load(std::memory_order_relaxed);
        if (seq & 1u) ++seq;
        s->sequence.store(seq + 1u, std::memory_order_release);
        s->flags.store(0, std::memory_order_relaxed);
        s->sequence.store(seq + 2u, std::memory_order_release);
    }

    void close() noexcept {
        if (snapshot_) {
            invalidate();
            ::munmap(snapshot_, kHardwareClockSnapshotBytes);
            snapshot_ = nullptr;
        }
    }

private:
    HardwareClockSnapshot* snapshot_ = nullptr;
};

// Open/map once off the audio callback. read() performs only a bounded number
// of lock-free loads; it never opens files or calls the kernel.
class HardwareClockReader {
public:
    HardwareClockReader() = default;
    HardwareClockReader(const HardwareClockReader&) = delete;
    HardwareClockReader& operator=(const HardwareClockReader&) = delete;
    ~HardwareClockReader() { close(); }

    bool open(const char* path = kHardwareClockSnapshotPath) noexcept {
        if (snapshot_) return true;
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        struct stat st{};
        if (::fstat(fd, &st) < 0 ||
            static_cast<size_t>(st.st_size) < kHardwareClockSnapshotBytes) {
            ::close(fd);
            return false;
        }
        void* memory = ::mmap(nullptr, kHardwareClockSnapshotBytes,
                              PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (memory == MAP_FAILED) return false;
        snapshot_ = static_cast<const HardwareClockSnapshot*>(memory);
        if (snapshot_->magic.load(std::memory_order_acquire) != kHardwareClockSnapshotMagic ||
            snapshot_->version.load(std::memory_order_acquire) != kHardwareClockSnapshotVersion) {
            // Do not retain a mapped-but-invalid snapshot: callers must be able to
            // retry after the HAL publisher initializes the shared-memory contract.
            ::munmap(const_cast<HardwareClockSnapshot*>(snapshot_), kHardwareClockSnapshotBytes);
            snapshot_ = nullptr;
            return false;
        }
        return true;
    }

    bool read(HardwareClockSample& out) const noexcept {
        const auto* s = snapshot_;
        if (!s || s->magic.load(std::memory_order_acquire) != kHardwareClockSnapshotMagic ||
            s->version.load(std::memory_order_acquire) != kHardwareClockSnapshotVersion)
            return false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const uint32_t before = s->sequence.load(std::memory_order_acquire);
            if (before == 0 || (before & 1u)) continue;
            if (s->flags.load(std::memory_order_relaxed) != kHardwareClockSnapshotActive)
                return false;
            HardwareClockSample sample;
            sample.streamId = s->streamId.load(std::memory_order_relaxed);
            sample.routeGeneration = s->routeGeneration.load(std::memory_order_relaxed);
            sample.presentedFrames = s->presentedFrames.load(std::memory_order_relaxed);
            sample.hardwareTimestampNs = s->hardwareTimestampNs.load(std::memory_order_relaxed);
            sample.nominalSampleRateHz = s->nominalSampleRateHz.load(std::memory_order_relaxed);
            const uint32_t after = s->sequence.load(std::memory_order_acquire);
            if (before == after && !(after & 1u)) {
                if (!sample.streamId || !sample.routeGeneration ||
                    !sample.hardwareTimestampNs || sample.nominalSampleRateHz < 8000 ||
                    sample.nominalSampleRateHz > 768000) return false;
                out = sample;
                return true;
            }
        }
        return false;
    }

    void close() noexcept {
        if (snapshot_) {
            ::munmap(const_cast<HardwareClockSnapshot*>(snapshot_), kHardwareClockSnapshotBytes);
            snapshot_ = nullptr;
        }
    }

private:
    const HardwareClockSnapshot* snapshot_ = nullptr;
};

} // namespace ivanna::audio
