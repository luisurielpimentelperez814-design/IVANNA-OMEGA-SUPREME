#pragma once
// Evidencia MEDIDA por procesador (HRTF / RIR / SAF) publicada por omega_effect.
// A diferencia del beacon (que solo dice "un bloque paso"), aqui cada procesador
// reporta contadores y energias medidas en SU propio camino de audio, y los
// coeficientes SAF efectivamente horneados. Un lector externo (scripts/read_processor_evidence.py)
// puede comparar dos lecturas y decidir si cada procesador esta aplicando de verdad.
// Seqlock: seq impar = escritura en curso. publish() es RT-safe (solo stores).
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ivanna {

inline constexpr const char* OMEGA_EFFECT_EVIDENCE_PATH = "/data/local/tmp/omega_effect_evidence";
inline constexpr uint32_t OMEGA_EFFECT_EVIDENCE_MAGIC = 0x4F454556u;  // "OEEV"
inline constexpr uint32_t OMEGA_EFFECT_EVIDENCE_VERSION = 1u;

struct alignas(8) EffectEvidence {
    uint32_t magic;
    uint32_t version;
    uint64_t seq;               // seqlock
    uint64_t hrtfCpuBlocks, hrtfDspBlocks, hrtfSafRefreshes, hrtfPoseApplies;
    uint64_t rirLoads, rirRelinks, rirActiveBlocks;
    double   hrtfInEnergy, hrtfOutEnergy;
    double   rirInEnergy, rirOutEnergy;
    float    hrtfAzimuthDeg, hrtfSafBiasDeg, hrtfCurvature, rirWet;
    float    rirBakedEarly, rirBakedDecorrel, rirBakedRt60Delta, rirWetScale;
    uint32_t sampleRate;
    uint32_t reserved;
};
static_assert(sizeof(EffectEvidence) <= 256, "EffectEvidence excede 256 B");

class EvidencePublisher {
public:
    // NO-RT.
    bool open(const char* path = OMEGA_EFFECT_EVIDENCE_PATH) noexcept {
        if (map_) return true;
        const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        if (fd < 0) return false;
        if (::ftruncate(fd, 256) < 0) { ::close(fd); return false; }
        void* a = ::mmap(nullptr, 256, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (a == MAP_FAILED) return false;
        map_ = static_cast<EffectEvidence*>(a);
        map_->magic = OMEGA_EFFECT_EVIDENCE_MAGIC;
        map_->version = OMEGA_EFFECT_EVIDENCE_VERSION;
        return true;
    }
    bool isOpen() const noexcept { return map_ != nullptr; }
    // RT-safe: sin syscalls ni asignacion.
    void publish(const EffectEvidence& e) noexcept {
        if (!map_) return;
        auto* seq = reinterpret_cast<std::atomic<uint64_t>*>(&map_->seq);
        const uint64_t s = seq->load(std::memory_order_relaxed);
        seq->store(s + 1, std::memory_order_release);
        EffectEvidence tmp = e;
        tmp.magic = OMEGA_EFFECT_EVIDENCE_MAGIC;
        tmp.version = OMEGA_EFFECT_EVIDENCE_VERSION;
        tmp.seq = s + 1;
        const uint64_t keep = tmp.seq;
        *map_ = tmp;
        map_->seq = keep;
        seq->store(s + 2, std::memory_order_release);
    }
private:
    EffectEvidence* map_ = nullptr;
};

inline EvidencePublisher& effectEvidence() noexcept {
    static EvidencePublisher p;
    return p;
}

}  // namespace ivanna
