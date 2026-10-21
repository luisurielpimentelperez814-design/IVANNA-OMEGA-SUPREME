#pragma once

#include "audio_hardware_clock.hpp"

#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ivanna::audio {

// Cross-process contract between the actual output HAL/PCM owner and the
// AudioFlinger effect. The producer MUST publish a hardware-correlated
// presentation timestamp and the matching presented-frame position.
// Never publish CLOCK_MONOTONIC sampled at effect callback entry.
inline constexpr const char* kHardwareClockSnapshotPath =
        "/data/local/tmp/ivanna_hw_clock_snapshot";
inline constexpr uint32_t kHardwareClockSnapshotMagic = 0x49564843u; // "IVHC"
inline constexpr size_t kHardwareClockSnapshotBytes = 64;

struct alignas(8) HardwareClockSnapshot {
    std::atomic<uint32_t> magic{0};
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint64_t> presentedFrames{0};
    std::atomic<uint64_t> hardwareTimestampNs{0};
    std::atomic<uint32_t> nominalSampleRateHz{0};
    std::atomic<uint32_t> reserved{0};
};
static_assert(sizeof(HardwareClockSnapshot) <= kHardwareClockSnapshotBytes);
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
        const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        if (fd < 0) return false;
        (void)::fchmod(fd, 0666);
        if (::ftruncate(fd, kHardwareClockSnapshotBytes) < 0) {
            ::close(fd);
            return false;
        }
        void* memory = ::mmap(nullptr, kHardwareClockSnapshotBytes,
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (memory == MAP_FAILED) return false;
        snapshot_ = static_cast<HardwareClockSnapshot*>(memory);
        snapshot_->presentedFrames.store(0, std::memory_order_relaxed);
        snapshot_->hardwareTimestampNs.store(0, std::memory_order_relaxed);
        snapshot_->nominalSampleRateHz.store(0, std::memory_order_relaxed);
        snapshot_->sequence.store(0, std::memory_order_relaxed);
        snapshot_->magic.store(kHardwareClockSnapshotMagic, std::memory_order_release);
        return true;
    }

    bool publish(const HardwareClockSample& sample) noexcept {
        auto* s = snapshot_;
        if (!s || sample.hardwareTimestampNs == 0 ||
            sample.nominalSampleRateHz < 8000 ||
            sample.nominalSampleRateHz > 768000) return false;
        uint32_t seq = s->sequence.load(std::memory_order_relaxed);
        if (seq & 1u) ++seq;
        s->sequence.store(seq + 1u, std::memory_order_release); // write in progress
        s->presentedFrames.store(sample.presentedFrames, std::memory_order_relaxed);
        s->hardwareTimestampNs.store(sample.hardwareTimestampNs, std::memory_order_relaxed);
        s->nominalSampleRateHz.store(sample.nominalSampleRateHz, std::memory_order_relaxed);
        s->sequence.store(seq + 2u, std::memory_order_release); // committed snapshot
        return true;
    }

    void close() noexcept {
        if (snapshot_) {
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
        return snapshot_->magic.load(std::memory_order_acquire) == kHardwareClockSnapshotMagic;
    }

    bool read(HardwareClockSample& out) const noexcept {
        const auto* s = snapshot_;
        if (!s || s->magic.load(std::memory_order_acquire) != kHardwareClockSnapshotMagic)
            return false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const uint32_t before = s->sequence.load(std::memory_order_acquire);
            if (before == 0 || (before & 1u)) continue;
            HardwareClockSample sample;
            sample.presentedFrames = s->presentedFrames.load(std::memory_order_relaxed);
            sample.hardwareTimestampNs = s->hardwareTimestampNs.load(std::memory_order_relaxed);
            sample.nominalSampleRateHz = s->nominalSampleRateHz.load(std::memory_order_relaxed);
            const uint32_t after = s->sequence.load(std::memory_order_acquire);
            if (before == after && !(after & 1u)) {
                if (!sample.hardwareTimestampNs || sample.nominalSampleRateHz < 8000 ||
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
