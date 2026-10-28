#pragma once
// asrc_state_bridge.hpp — canal IPC real para el estado del core ASRC software
// (AsrcClockController, app/src/main/cpp/dsp/asrc_clock_controller.hpp).
//
// FIX (2026-10-24): AsrcClockController existía, compilaba y tenía tests
// propios desde antes, pero NUNCA se instanciaba en el daemon ni en ningún
// sitio fuera de su propio archivo de test — cero consumidores reales. El
// usuario instalaba el módulo y no veía ningún cambio porque, literalmente,
// nada lo ejecutaba. Este archivo publica el AsrcControlSnapshot en un mmap
// propio de 64 B (mismo patrón seqlock que audio_hardware_clock_bridge.hpp,
// ya probado en producción desde 2.5.130) para que el daemon (writer) y la
// app vía JNI (reader) lo compartan sin locks ni syscalls en el hot path.
//
// QUÉ NO ES: AsrcClockController sigue siendo un servo software sobre el
// resampler propio de la app (igual que AudioTrackBufferPll) — no identifica
// ni acciona ningún ASRC físico del Moto G85. El actuador físico sigue sin
// identificar; ver hal_clock_producer.hpp.

#include "../dsp/asrc_clock_controller.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace ivanna::audio {

inline constexpr const char* kAsrcStateSnapshotPath =
        "/data/vendor/ivanna_audio/asrc_state";
inline constexpr uint32_t kAsrcStateSnapshotMagic = 0x49415343u; // "IASC"
inline constexpr size_t kAsrcStateSnapshotBytes = 64;

struct alignas(8) AsrcStateSnapshot {
    std::atomic<uint32_t> magic{0};
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> state{0};        // ivanna::AsrcState como entero
    std::atomic<uint32_t> driftPpmBits{0};      // float reinterpretado a bits
    std::atomic<uint32_t> targetPpmBits{0};
    std::atomic<uint32_t> appliedRatioBits{0};
    // Split into lock-free 32-bit words for 32-bit ARM portability.
    std::atomic<uint32_t> publishedAtMonotonicNsLo{0};
    std::atomic<uint32_t> publishedAtMonotonicNsHi{0};
    // Texto en palabras atomicas: el seqlock no elimina data races de bytes no atomicos.
    std::array<std::atomic<uint32_t>, 6> reasonWords{}; // 24 bytes, NUL-terminado
};
static_assert(sizeof(AsrcStateSnapshot) <= kAsrcStateSnapshotBytes,
              "AsrcStateSnapshot excede 64 B");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "asrc state IPC requiere atomicos de 32 bit lock-free");

inline uint32_t floatToBits(float v) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}
inline float bitsToFloat(uint32_t bits) noexcept {
    float v = 0.0f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

// Writer: solo el daemon (root) lo usa. publish() es allocation-free y
// syscall-free tras open(), apto para llamarse cada ciclo de sondeo (250 ms).
class AsrcStatePublisher {
public:
    AsrcStatePublisher() = default;
    AsrcStatePublisher(const AsrcStatePublisher&) = delete;
    AsrcStatePublisher& operator=(const AsrcStatePublisher&) = delete;
    ~AsrcStatePublisher() { close(); }

    bool open(const char* path = kAsrcStateSnapshotPath) noexcept {
        if (snapshot_) return true;
        const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0660);
        if (fd < 0) return false;
        (void)::fchmod(fd, 0660);
        if (::ftruncate(fd, kAsrcStateSnapshotBytes) < 0) {
            ::close(fd);
            return false;
        }
        void* memory = ::mmap(nullptr, kAsrcStateSnapshotBytes,
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (memory == MAP_FAILED) return false;
        snapshot_ = static_cast<AsrcStateSnapshot*>(memory);
        snapshot_->sequence.store(0, std::memory_order_relaxed);
        snapshot_->magic.store(kAsrcStateSnapshotMagic, std::memory_order_release);
        return true;
    }

    bool publish(const ::ivanna::dsp::AsrcControlSnapshot& s) noexcept {
        auto* p = snapshot_;
        const uint32_t state = static_cast<uint32_t>(s.state);
        if (!p || state > static_cast<uint32_t>(::ivanna::dsp::AsrcState::FAULT) ||
            !std::isfinite(s.driftPpm) || !std::isfinite(s.targetPpm) ||
            !std::isfinite(s.appliedRatio) || s.appliedRatio <= 0.0) return false;
        timespec now{};
        if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return false;
        const uint64_t publishedNs = static_cast<uint64_t>(now.tv_sec) * 1000000000ULL +
                                     static_cast<uint64_t>(now.tv_nsec);
        uint32_t seq = p->sequence.load(std::memory_order_relaxed);
        if (seq & 1u) ++seq;
        p->sequence.store(seq + 1u, std::memory_order_release);
        p->state.store(static_cast<uint32_t>(s.state), std::memory_order_relaxed);
        p->driftPpmBits.store(floatToBits(static_cast<float>(s.driftPpm)), std::memory_order_relaxed);
        p->targetPpmBits.store(floatToBits(static_cast<float>(s.targetPpm)), std::memory_order_relaxed);
        p->appliedRatioBits.store(floatToBits(static_cast<float>(s.appliedRatio)), std::memory_order_relaxed);
        p->publishedAtMonotonicNsLo.store(static_cast<uint32_t>(publishedNs), std::memory_order_relaxed);
        p->publishedAtMonotonicNsHi.store(static_cast<uint32_t>(publishedNs >> 32), std::memory_order_relaxed);
        char reason[24]{};
        if (s.reason) std::strncpy(reason, s.reason, sizeof(reason) - 1);
        uint32_t words[6]{};
        std::memcpy(words, reason, sizeof(reason));
        for (size_t i = 0; i < p->reasonWords.size(); ++i)
            p->reasonWords[i].store(words[i], std::memory_order_relaxed);
        p->sequence.store(seq + 2u, std::memory_order_release);
        return true;
    }

    void close() noexcept {
        if (snapshot_) { ::munmap(snapshot_, kAsrcStateSnapshotBytes); snapshot_ = nullptr; }
    }

private:
    AsrcStateSnapshot* snapshot_ = nullptr;
};

struct AsrcStateSample {
    ::ivanna::dsp::AsrcState state = ::ivanna::dsp::AsrcState::INIT;
    double driftPpm = 0.0;
    double targetPpm = 0.0;
    double appliedRatio = 1.0;
    uint64_t publishedAtMonotonicNs = 0;
    char reason[24]{};
};

// Reader: lo usa la app (JNI), proceso separado del daemon. open() se puede
// reintentar; si el publisher aún no existe simplemente falla y el llamador
// debe declarar "sin productor validado", nunca simular un valor.
class AsrcStateReader {
public:
    AsrcStateReader() = default;
    AsrcStateReader(const AsrcStateReader&) = delete;
    AsrcStateReader& operator=(const AsrcStateReader&) = delete;
    ~AsrcStateReader() { close(); }

    bool open(const char* path = kAsrcStateSnapshotPath) noexcept {
        if (snapshot_) return true;
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        struct stat st{};
        if (::fstat(fd, &st) < 0 || static_cast<size_t>(st.st_size) < kAsrcStateSnapshotBytes) {
            ::close(fd);
            return false;
        }
        void* memory = ::mmap(nullptr, kAsrcStateSnapshotBytes, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (memory == MAP_FAILED) return false;
        snapshot_ = static_cast<const AsrcStateSnapshot*>(memory);
        if (snapshot_->magic.load(std::memory_order_acquire) != kAsrcStateSnapshotMagic) {
            ::munmap(const_cast<AsrcStateSnapshot*>(snapshot_), kAsrcStateSnapshotBytes);
            snapshot_ = nullptr;
            return false;
        }
        return true;
    }

    // Persisted mmap state must not masquerade as live after producer failure.
    bool read(AsrcStateSample& out,
              uint64_t maxAgeNs = 1000000000ULL) const noexcept {
        const auto* s = snapshot_;
        if (!s || s->magic.load(std::memory_order_acquire) != kAsrcStateSnapshotMagic)
            return false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const uint32_t before = s->sequence.load(std::memory_order_acquire);
            if (before == 0 || (before & 1u)) continue;
            AsrcStateSample sample;
            sample.state = static_cast<::ivanna::dsp::AsrcState>(s->state.load(std::memory_order_relaxed));
            sample.driftPpm = bitsToFloat(s->driftPpmBits.load(std::memory_order_relaxed));
            sample.targetPpm = bitsToFloat(s->targetPpmBits.load(std::memory_order_relaxed));
            sample.appliedRatio = bitsToFloat(s->appliedRatioBits.load(std::memory_order_relaxed));
            const uint32_t publishedLo = s->publishedAtMonotonicNsLo.load(std::memory_order_relaxed);
            const uint32_t publishedHi = s->publishedAtMonotonicNsHi.load(std::memory_order_relaxed);
            sample.publishedAtMonotonicNs = (static_cast<uint64_t>(publishedHi) << 32) | publishedLo;
            uint32_t words[6]{};
            for (size_t i = 0; i < s->reasonWords.size(); ++i)
                words[i] = s->reasonWords[i].load(std::memory_order_relaxed);
            std::memcpy(sample.reason, words, sizeof(sample.reason));
            sample.reason[sizeof(sample.reason) - 1] = '\0';
            const uint32_t after = s->sequence.load(std::memory_order_acquire);
            const uint32_t state = static_cast<uint32_t>(sample.state);
            if (before == after && !(after & 1u) &&
                state <= static_cast<uint32_t>(::ivanna::dsp::AsrcState::FAULT) &&
                std::isfinite(sample.driftPpm) && std::isfinite(sample.targetPpm) &&
                std::isfinite(sample.appliedRatio) && sample.appliedRatio > 0.0 &&
                sample.publishedAtMonotonicNs != 0) {
                timespec now{};
                if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return false;
                const uint64_t nowNs = static_cast<uint64_t>(now.tv_sec) * 1000000000ULL +
                                       static_cast<uint64_t>(now.tv_nsec);
                if (sample.publishedAtMonotonicNs > nowNs ||
                    nowNs - sample.publishedAtMonotonicNs > maxAgeNs) return false;
                out = sample;
                return true;
            }
        }
        return false;
    }

    void close() noexcept {
        if (snapshot_) { ::munmap(const_cast<AsrcStateSnapshot*>(snapshot_), kAsrcStateSnapshotBytes); snapshot_ = nullptr; }
    }

private:
    const AsrcStateSnapshot* snapshot_ = nullptr;
};

inline const char* asrcStateName(::ivanna::dsp::AsrcState s) noexcept {
    switch (s) {
        case ::ivanna::dsp::AsrcState::INIT: return "init";
        case ::ivanna::dsp::AsrcState::WAITING_FOR_SAMPLES: return "esperando-muestras";
        case ::ivanna::dsp::AsrcState::TRACKING: return "corrigiendo";
        case ::ivanna::dsp::AsrcState::HOLD: return "en-espera";
        case ::ivanna::dsp::AsrcState::BYPASS: return "bypass";
        case ::ivanna::dsp::AsrcState::FAULT: return "fallo";
    }
    return "desconocido";
}

} // namespace ivanna::audio
