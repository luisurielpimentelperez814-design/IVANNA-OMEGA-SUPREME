#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — EJE 5: ShmPipelineArbitrator & FarrowOrder5Delay (C++23)
// Arbitraje Lock-Free en Memoria Compartida (SHM) con exclusión CAS sobre
// owner_pid (bypass AudioFlinger estilo eBPF/XDP) y Alineación de Fase MSO
// (Oliva Superior Medial) mediante Filtros Polinómicos de Farrow de 5º Orden
// con evaluación anidada de Horner (precisión sub-nanosegundo).
// ═══════════════════════════════════════════════════════════════════════════════

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include "SupremeAcousticContinuity.hpp"
#include "SupremeTransitionEnvelope.hpp"

#if defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ivanna::supreme {

/**
 * @class FarrowOrder5Delay
 * @brief Interpolador polinómico de Farrow de 5º orden (6 taps Lagrange) evaluado
 *        mediante el esquema anidado de Horner:
 *        y(n - d) = ((((C_5 d + C_4) d + C_3) d + C_2) d + C_1) d + C_0
 *        Garantiza planitud de magnitud máxima y precisión de fase MSO a nivel de nanosegundos.
 */
class alignas(64) FarrowOrder5Delay {
public:
    static constexpr size_t ORDER = 5;
    static constexpr size_t NUM_TAPS = ORDER + 1; // 6 muestras de línea de retardo

    FarrowOrder5Delay() noexcept {
        delayLine_.fill(0.0f);
        hasRealHistory_ = false;
    }

    /**
     * @brief Conserva las últimas NUM_TAPS (6) muestras reales del bloque de audio
     *        durante Soft Suspension (costo O(1) = 6 floats por bloque, sin evaluar
     *        el polinomio de Horner), garantizando que al reactivar el motor la
     *        línea de retardo contenga la historia física exacta de la onda.
     */
    [[gnu::always_inline]] inline void preserveBlockTail(
        const float* __restrict input,
        size_t numSamples) noexcept
    {
        if (!input || numSamples == 0) return;
        const size_t copyCount = std::min(NUM_TAPS, numSamples);
        if (copyCount < NUM_TAPS) {
            for (size_t k = NUM_TAPS - 1; k >= copyCount; --k) {
                delayLine_[k] = delayLine_[k - copyCount];
            }
        }
        for (size_t k = 0; k < copyCount; ++k) {
            delayLine_[k] = sanitize(input[numSamples - 1 - k]);
        }
        hasRealHistory_ = true;
    }

    [[gnu::always_inline]] inline void validateState(SupremeStateContinuityManager& mgr) noexcept {
        mgr.validateStateArray(delayLine_);
    }

    [[gnu::always_inline]] inline float sanitize(float x) const noexcept {
        return (std::isfinite(x) && std::fabs(x) > 1.0e-30f) ? x : 0.0f;
    }

    /**
     * @brief Procesa una muestra con retardo fraccional continuo μ ∈ [0.0, 1.0]
     *        centrado en el tap intermedio (retardo base entero = 2 muestras)
     *        sobre la historia acústica real conservada.
     */
    [[gnu::always_inline]] inline float processSample(float input, float fractionalDelay) noexcept {
        const float cleanIn = sanitize(input);
        if (!hasRealHistory_) {
            // Arranque en frío del stream (bloque 0, muestra 0): inicializar línea
            // con la muestra entrante real; a partir de aquí la historia es inmortal.
            for (size_t k = 0; k < NUM_TAPS; ++k) {
                delayLine_[k] = cleanIn;
            }
            hasRealHistory_ = true;
        } else {
            // Desplazamiento de registro en línea de caché L1 (6 floats = 24 bytes)
            for (size_t k = NUM_TAPS - 1; k > 0; --k) {
                delayLine_[k] = delayLine_[k - 1];
            }
            delayLine_[0] = cleanIn;
        }

        // Retardo fraccional acotado alrededor del centro simétrico (entre x[2] y x[3])
        const float d = std::clamp(fractionalDelay, 0.0f, 1.0f);

        const float x0 = delayLine_[0];
        const float x1 = delayLine_[1];
        const float x2 = delayLine_[2];
        const float x3 = delayLine_[3];
        const float x4 = delayLine_[4];
        const float x5 = delayLine_[5];

        // Coeficientes exactos de la matriz de Farrow de Lagrange de 5º orden
        // centrada en el intervalo [x2, x3]:
        const float c0 = x2;
        const float c1 = (1.0f / 20.0f) * x0 - (1.0f / 2.0f) * x1 - (1.0f / 3.0f) * x2
                       + 1.0f * x3 - (1.0f / 4.0f) * x4 + (1.0f / 30.0f) * x5;
        const float c2 = -(1.0f / 24.0f) * x0 + (2.0f / 3.0f) * x1 - (5.0f / 4.0f) * x2
                       + (2.0f / 3.0f) * x3 - (1.0f / 24.0f) * x4;
        const float c3 = -(1.0f / 24.0f) * x0 - (1.0f / 24.0f) * x1 + (5.0f / 12.0f) * x2
                       - (7.0f / 12.0f) * x3 + (7.0f / 24.0f) * x4 - (1.0f / 24.0f) * x5;
        const float c4 = (1.0f / 24.0f) * x0 - (1.0f / 6.0f) * x1 + (1.0f / 4.0f) * x2
                       - (1.0f / 6.0f) * x3 + (1.0f / 24.0f) * x4;
        const float c5 = -(1.0f / 120.0f) * x0 + (1.0f / 24.0f) * x1 - (1.0f / 12.0f) * x2
                       + (1.0f / 12.0f) * x3 - (1.0f / 24.0f) * x4 + (1.0f / 120.0f) * x5;

        // Polinomio anidado de Horner de 5º grado (5 FMA secuenciales)
        float acc = c5;
        acc = acc * d + c4;
        acc = acc * d + c3;
        acc = acc * d + c2;
        acc = acc * d + c1;
        acc = acc * d + c0;

        return sanitize(acc);
    }

    [[nodiscard]] const std::array<float, NUM_TAPS>& history() const noexcept { return delayLine_; }
    [[nodiscard]] float historyEnergy() const noexcept {
        float e = 0.0f;
        for (size_t k = 0; k < NUM_TAPS; ++k) e += std::fabs(delayLine_[k]);
        return e;
    }

private:
    alignas(32) std::array<float, NUM_TAPS> delayLine_{};
    bool hasRealHistory_{false};
};

/**
 * @struct ShmArbitrationControlBlock
 * @brief Bloque de control en memoria compartida (SHM) alineado a líneas de caché
 *        de 64 bytes para arbitraje lock-free entre el daemon Magisk (`ivanna_daemon`)
 *        y el efecto HAL (`libomega_effect.so`), evitando el doble procesamiento.
 */
struct alignas(64) ShmArbitrationControlBlock {
    static constexpr uint32_t MAGIC = 0x4F4D4547u; // "OMEG"
    static constexpr uint32_t RING_CAPACITY = 1024; // Potencia de 2 para máscara bit a bit

    // Línea de caché 0: Propiedad y estado de arbitraje CAS
    alignas(64) std::atomic<uint32_t> magic{MAGIC};
    std::atomic<int32_t>  owner_pid{0};          // 0 = Libre; >0 = PID del propietario activo
    std::atomic<uint64_t> lease_epoch_ns{0};     // Marca de tiempo monotónica del último latido
    std::atomic<bool>     ebpf_bypass_active{false};
    std::atomic<float>    mso_itd_nanoseconds{0.0f};

    // Línea de caché 1: Índices SPSC del Ringbuffer (evita false sharing con consumidor)
    alignas(64) std::atomic<uint32_t> write_idx{0};

    // Línea de caché 2: Índice de lectura del consumidor
    alignas(64) std::atomic<uint32_t> read_idx{0};

    // Línea de caché 3+: Buffer circular intercalado L/R
    alignas(64) std::array<float, RING_CAPACITY * 2> pcm_ring{};

    /**
     * @brief Adquiere la propiedad exclusiva del pipeline de audio mediante
     *        Compare-And-Swap (CAS) atómico sin bloqueo.
     * @param candidatePid PID del proceso solicitante (daemon o HAL).
     * @param nowNs Timestamp monotónico actual en nanosegundos.
     * @param staleTimeoutNs Tiempo tras el cual un lease silencioso se considera expirado.
     */
    bool tryAcquireOwnership(int32_t candidatePid, uint64_t nowNs, uint64_t staleTimeoutNs = 50'000'000ULL) noexcept {
        if (candidatePid <= 0) return false;

        int32_t currentOwner = owner_pid.load(std::memory_order_acquire);
        if (currentOwner == candidatePid) {
            lease_epoch_ns.store(nowNs, std::memory_order_release);
            return true;
        }

        // Caso 1: Pipeline libre (owner_pid == 0)
        int32_t expectedFree = 0;
        if (owner_pid.compare_exchange_strong(
                expectedFree, candidatePid,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            lease_epoch_ns.store(nowNs, std::memory_order_release);
            return true;
        }

        // Caso 2: Preempción sin bloqueo si el propietario anterior expiró su lease (>50 ms sin heartbeat)
        const uint64_t lastEpoch = lease_epoch_ns.load(std::memory_order_acquire);
        if (nowNs > lastEpoch && (nowNs - lastEpoch) > staleTimeoutNs) {
            if (owner_pid.compare_exchange_strong(
                    currentOwner, candidatePid,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                lease_epoch_ns.store(nowNs, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Libera la propiedad del pipeline si y solo si el invocador es el `owner_pid` actual.
     */
    bool releaseOwnership(int32_t callerPid) noexcept {
        int32_t expected = callerPid;
        if (owner_pid.compare_exchange_strong(
                expected, 0,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            ebpf_bypass_active.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

    /**
     * @brief Escribe frames estéreo en el ringbuffer SPSC lock-free.
     */
    size_t pushStereoFrames(const float* __restrict left, const float* __restrict right, size_t frames) noexcept {
        const uint32_t w = write_idx.load(std::memory_order_relaxed);
        const uint32_t r = read_idx.load(std::memory_order_acquire);
        const uint32_t avail = RING_CAPACITY - (w - r);
        const size_t toWrite = std::min(frames, static_cast<size_t>(avail));

        for (size_t i = 0; i < toWrite; ++i) {
            const uint32_t slot = (w + static_cast<uint32_t>(i)) & (RING_CAPACITY - 1);
            pcm_ring[slot * 2]     = left[i];
            pcm_ring[slot * 2 + 1] = right[i];
        }
        write_idx.store(w + static_cast<uint32_t>(toWrite), std::memory_order_release);
        return toWrite;
    }

    /**
     * @brief Extrae frames estéreo del ringbuffer SPSC lock-free.
     */
    size_t popStereoFrames(float* __restrict left, float* __restrict right, size_t frames) noexcept {
        const uint32_t r = read_idx.load(std::memory_order_relaxed);
        const uint32_t w = write_idx.load(std::memory_order_acquire);
        const uint32_t ready = w - r;
        const size_t toRead = std::min(frames, static_cast<size_t>(ready));

        for (size_t i = 0; i < toRead; ++i) {
            const uint32_t slot = (r + static_cast<uint32_t>(i)) & (RING_CAPACITY - 1);
            left[i]  = pcm_ring[slot * 2];
            right[i] = pcm_ring[slot * 2 + 1];
        }
        read_idx.store(r + static_cast<uint32_t>(toRead), std::memory_order_release);
        return toRead;
    }
};

/**
 * @class SupremeMsoFarrowArbitrator
 * @brief Orquestador en tiempo real del Eje 5: Arbitraje SHM lockless CAS
 *        (owner_pid + bypass eBPF/XDP) y alineación interaural MSO mediante
 *        un par estéreo de filtros polinómicos de Farrow de 5º Orden.
 */
class alignas(64) SupremeMsoFarrowArbitrator {
public:
    SupremeMsoFarrowArbitrator() noexcept {
        attachSharedMemory();
        transitionEnv_.configure(48000.0f, 8.0f, 18.0f, 35.0f);
        continuityMgr_.configure(48000.0f, 5.0f);
        reset();
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        transitionEnv_.configure(sampleRate_, 8.0f, 18.0f, 35.0f);
        continuityMgr_.configure(sampleRate_, 5.0f);
        reset();
    }

    ~SupremeMsoFarrowArbitrator() noexcept {
#if defined(__linux__) || defined(__ANDROID__)
        if (mappedBlock_ && mappedBlock_ != &shmLocal_) {
            ::munmap(mappedBlock_, sizeof(ShmArbitrationControlBlock));
            mappedBlock_ = &shmLocal_;
        }
        if (shmFd_ >= 0) {
            ::close(shmFd_);
            shmFd_ = -1;
        }
#endif
    }

    SupremeMsoFarrowArbitrator(const SupremeMsoFarrowArbitrator&) = delete;
    SupremeMsoFarrowArbitrator& operator=(const SupremeMsoFarrowArbitrator&) = delete;

    void reset() noexcept {
        smoothItdNs_ = msoItdNs_.load(std::memory_order_relaxed);
        const bool en = enabled_.load(std::memory_order_relaxed) &&
                        !thermalBypass_.load(std::memory_order_relaxed);
        transitionEnv_.setImmediate(en ? 1.0f : 0.0f);
        farrowL_.validateState(continuityMgr_);
        farrowR_.validateState(continuityMgr_);
        continuityMgr_.validateState();
    }

    void preserveAcousticState(const float* __restrict left, const float* __restrict right, size_t numSamples) noexcept {
        continuityMgr_.preserveState(left, right, numSamples);
        farrowL_.preserveBlockTail(left, numSamples);
        farrowR_.preserveBlockTail(right, numSamples);
        farrowL_.validateState(continuityMgr_);
        farrowR_.validateState(continuityMgr_);
        continuityMgr_.validateState();
    }

    bool isCrossProcessShmMapped() const noexcept {
        return mappedBlock_ != &shmLocal_;
    }

    void setEnabled(bool en) noexcept {
        enabled_.store(en, std::memory_order_release);
        if (!en && transitionEnv_.renderedBlocks == 0u) {
            transitionEnv_.setImmediate(0.0f);
        }
    }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    void setThermalBypass(bool skip) noexcept { thermalBypass_.store(skip, std::memory_order_release); }
    bool isThermalBypass() const noexcept { return thermalBypass_.load(std::memory_order_acquire); }
    void setTransitionTimesMs(float attackMs, float releaseMs, float thermalMs = 35.0f) noexcept {
        transitionEnv_.configure(sampleRate_, attackMs, releaseMs, thermalMs);
    }
    const SupremeTransitionEnvelope& transitionEnvelope() const noexcept { return transitionEnv_; }
    const SupremeStateContinuityManager& continuityManager() const noexcept { return continuityMgr_; }
    float currentTransitionGain() const noexcept { return transitionEnv_.currentGain; }
    float preservedStateEnergy() const noexcept {
        return farrowL_.historyEnergy() + farrowR_.historyEnergy();
    }

    void setMsoItdNanoseconds(float ns) noexcept {
        const float clamped = std::clamp(ns, -750000.0f, 750000.0f);
        msoItdNs_.store(clamped, std::memory_order_release);
        shm().mso_itd_nanoseconds.store(clamped, std::memory_order_relaxed);
        if (transitionEnv_.renderedBlocks == 0u) {
            smoothItdNs_ = clamped;
        }
    }

    float msoItdNanoseconds() const noexcept {
        return msoItdNs_.load(std::memory_order_acquire);
    }

    void setEbpfBypassActive(bool active) noexcept {
        shm().ebpf_bypass_active.store(active, std::memory_order_release);
    }

    bool isEbpfBypassActive() const noexcept {
        return shm().ebpf_bypass_active.load(std::memory_order_acquire);
    }

    bool tryAcquireOwnership(int32_t pid, uint64_t nowNs) noexcept {
        return shm().tryAcquireOwnership(pid, nowNs);
    }

    bool releaseOwnership(int32_t pid) noexcept {
        return shm().releaseOwnership(pid);
    }

    int32_t ownerPid() const noexcept {
        return shm().owner_pid.load(std::memory_order_acquire);
    }

    uint64_t leaseEpochNs() const noexcept {
        return shm().lease_epoch_ns.load(std::memory_order_acquire);
    }

    void process(float* __restrict left, float* __restrict right, size_t numSamples, float sampleRate = 48000.0f) noexcept {
        if (!left || !right || numSamples == 0) return;
        const float sr = (sampleRate > 8000.0f) ? sampleRate : sampleRate_;
        if (std::fabs(sr - transitionEnv_.sampleRate) > 1.0f) {
            sampleRate_ = sr;
            transitionEnv_.configure(sr, transitionEnv_.attack_ms, transitionEnv_.release_ms, transitionEnv_.thermal_ms);
        }

        const bool thermSkip = thermalBypass_.load(std::memory_order_relaxed);
        const bool isThermChange = (thermSkip != lastThermalBypass_);
        lastThermalBypass_ = thermSkip;
        const bool wantOn = enabled_.load(std::memory_order_relaxed) && !thermSkip;
        const float targetEnv = wantOn ? 1.0f : 0.0f;
        const TransitionProfile profile = (isThermChange || (thermSkip && transitionEnv_.isTransitioning()))
            ? TransitionProfile::Thermal
            : TransitionProfile::Standard;
        const bool wasSilent = transitionEnv_.isSilent();
        if (!transitionEnv_.beginBlock(targetEnv, profile)) {
            smoothItdNs_ = msoItdNs_.load(std::memory_order_relaxed);
            // NIVEL 2 — Soft Suspension: conservar las últimas 6 muestras reales en
            // la línea de retardo de Farrow sin evaluar Horner (0 pérdida de historia)
            preserveAcousticState(left, right, numSamples);
            continuityMgr_.suspend(left, right, numSamples);
            return;
        }
        if (wasSilent) {
            // NIVEL 3 — Smooth State Resume: recuperación continua desde la historia
            // real conservada en farrowL_/farrowR_ (jamás se usa seedConstant ni reset)
            continuityMgr_.resume();
            farrowL_.validateState(continuityMgr_);
            farrowR_.validateState(continuityMgr_);
        }

        const float targetItdNs = msoItdNs_.load(std::memory_order_relaxed);

        for (size_t i = 0; i < numSamples; ++i) {
            const float resumeFactor = continuityMgr_.nextResumeFactor();
            smoothItdNs_ += (0.005f * resumeFactor + 0.001f) * (targetItdNs - smoothItdNs_);
            // Conversión de nanosegundos a fracción de muestra diferencial L/R alrededor de 0.5 muestras
            const float deltaSamples = (smoothItdNs_ * 1.0e-9f) * sr;
            const float fracL = std::clamp(0.5f - 0.5f * deltaSamples, 0.0f, 1.0f);
            const float fracR = std::clamp(0.5f + 0.5f * deltaSamples, 0.0f, 1.0f);

            const float dryL = std::isfinite(left[i])  ? left[i]  : 0.0f;
            const float dryR = std::isfinite(right[i]) ? right[i] : 0.0f;
            const float wetL = farrowL_.processSample(dryL, fracL);
            const float wetR = farrowR_.processSample(dryR, fracR);

            const float env = transitionEnv_.nextSample();
            left[i]  = SupremeTransitionEnvelope::mixSample(dryL, wetL, env);
            right[i] = SupremeTransitionEnvelope::mixSample(dryR, wetR, env);
        }
        continuityMgr_.preserveState(left, right, numSamples);
        if (transitionEnv_.isSilent()) {
            continuityMgr_.suspend(left, right, numSamples);
        }
    }

private:
    [[gnu::always_inline]] inline ShmArbitrationControlBlock& shm() noexcept {
        return *mappedBlock_;
    }
    [[gnu::always_inline]] inline const ShmArbitrationControlBlock& shm() const noexcept {
        return *mappedBlock_;
    }

    void attachSharedMemory() noexcept {
        mappedBlock_ = &shmLocal_;
#if defined(__linux__) || defined(__ANDROID__)
        static constexpr const char* kCandidatePaths[] = {
            "/dev/shm/omega_supreme_arb_v1",
            "/data/adb/ivanna_omega/omega_supreme_arb_v1",
            "/tmp/omega_supreme_arb_v1"
        };
        for (const char* path : kCandidatePaths) {
            const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
            if (fd < 0) continue;
            if (::ftruncate(fd, static_cast<off_t>(sizeof(ShmArbitrationControlBlock))) != 0) {
                ::close(fd);
                continue;
            }
            void* ptr = ::mmap(
                nullptr,
                sizeof(ShmArbitrationControlBlock),
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                0);
            if (ptr == MAP_FAILED || !ptr) {
                ::close(fd);
                continue;
            }
            auto* blk = static_cast<ShmArbitrationControlBlock*>(ptr);
            uint32_t expectedMagic = ShmArbitrationControlBlock::MAGIC;
            if (blk->magic.load(std::memory_order_acquire) != expectedMagic) {
                blk->owner_pid.store(0, std::memory_order_relaxed);
                blk->lease_epoch_ns.store(0, std::memory_order_relaxed);
                blk->ebpf_bypass_active.store(false, std::memory_order_relaxed);
                blk->mso_itd_nanoseconds.store(0.0f, std::memory_order_relaxed);
                blk->write_idx.store(0, std::memory_order_relaxed);
                blk->read_idx.store(0, std::memory_order_relaxed);
                blk->magic.store(expectedMagic, std::memory_order_release);
            }
            shmFd_ = fd;
            mappedBlock_ = blk;
            break;
        }
#endif
    }

    FarrowOrder5Delay farrowL_{};
    FarrowOrder5Delay farrowR_{};
    SupremeTransitionEnvelope transitionEnv_{};
    SupremeStateContinuityManager continuityMgr_{};
    ShmArbitrationControlBlock shmLocal_{};
    ShmArbitrationControlBlock* mappedBlock_{&shmLocal_};
    int shmFd_{-1};
    float sampleRate_{48000.0f};
    float smoothItdNs_{0.0f};
    bool lastThermalBypass_{false};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> thermalBypass_{false};
    std::atomic<float> msoItdNs_{0.0f};
};

} // namespace ivanna::supreme
