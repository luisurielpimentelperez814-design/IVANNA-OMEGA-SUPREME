#pragma once
/**
 * ============================================================================
 * IVANNA-OMEGA-SUPREME — KERNEL-LEVEL ACOUSTIC INTELLIGENCE ENGINE
 * Componente: IvannaTinyMLKernel.hpp (Reemplazo Integral de YAMNet)
 * ============================================================================
 * Restricciones de Tiempo Real Estricto (RT-Safe):
 *   1. CERO llamadas a malloc(), free(), new, delete o dynamic allocation.
 *   2. CERO mutexes, semáforos, futexes, condition_variables o llamadas de kernel.
 *   3. Alineación forzada a líneas de caché L1 (64 bytes) para erradicar False Sharing.
 *   4. Triple Buffering Wait-Free con std::atomic<T*>::exchange (C++20/C++23 acquire-release).
 *   5. Vectorización SIMD explícita con ARM NEON (AArch64).
 * ============================================================================
 */

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <thread>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IVANNA_ALIGNED_SIMD alignas(16)
#else
#define IVANNA_ALIGNED_SIMD alignas(16)
#endif

#define IVANNA_CACHE_LINE 64

namespace ivanna::tinyml {

// Clases de contexto acústico nativas
enum class AcousticSceneId : uint8_t {
    Unknown = 0,
    Music   = 1,
    Movie   = 2,
    Game    = 3,
    Voice   = 4,
    Ambient = 5
};

// Vector acústico continuo proyectado al motor DSP Anti-Dolby
struct alignas(IVANNA_CACHE_LINE) AcousticControlVector {
    std::array<float, 6> classProbabilities{};
    AcousticSceneId dominantScene{AcousticSceneId::Unknown};
    float confidence{0.0f};
    float sceneEnergyRms{0.0f};

    // Parámetros continuos de modulación física
    float targetWidenerMultiplier{1.0f}; // Mid/Side Decorrelation
    float targetVocalPresenceDb{0.0f};   // Formant boost 2-4kHz
    float targetSubExciterDrive{0.0f};   // Sub-bass reconstruction
    float targetSpatialSpread{1.0f};     // WFS curvature bias
    float antiDolbyIntensity{1.0f};      // Dynamic suppression of pumping artifacts
    uint64_t inferenceEpochNs{0};        // Timestamp de frescura del vector
    bool isValid{false};
};

// Hiperparámetros del Front-End Espectral
inline constexpr size_t kSampleRateInference = 16000;
inline constexpr size_t kDecimateFactor      = 3; // 48kHz / 3 = 16kHz
inline constexpr size_t kFftWindowSize       = 512;
inline constexpr size_t kFftSpectrumBins     = (kFftWindowSize / 2) + 1; // 257 bins
inline constexpr size_t kMelFilterBands      = 64;
inline constexpr size_t kHopLengthSamples    = 170; // ~10.6 ms hop
inline constexpr size_t kHiddenStates        = 32;
inline constexpr size_t kRingCapacity        = 16384; // Potencia de 2 obligatoria

// ============================================================================
// Lock-Free & Wait-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
// ============================================================================
template <typename T, size_t Capacity>
class alignas(IVANNA_CACHE_LINE) SpscAudioRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SpscAudioRingBuffer() : m_head(0), m_tail(0) {}

    // Productor (Audio Thread): RT-Safe, Wait-Free O(1)
    [[nodiscard]] inline bool push(const T* __restrict src, size_t count) noexcept {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);
        const size_t currentTail = m_tail.load(std::memory_order_acquire);

        // Comprobación de capacidad disponible sin desbordamiento
        if (Capacity - (currentHead - currentTail) < count) [[unlikely]] {
            return false; // Evita saturación y dropouts
        }

        const size_t mask = Capacity - 1;
        const size_t idx  = currentHead & mask;

        if (idx + count <= Capacity) [[likely]] {
            std::memcpy(&m_storage[idx], src, count * sizeof(T));
        } else {
            const size_t firstPart = Capacity - idx;
            std::memcpy(&m_storage[idx], src, firstPart * sizeof(T));
            std::memcpy(&m_storage[0], src + firstPart, (count - firstPart) * sizeof(T));
        }

        m_head.store(currentHead + count, std::memory_order_release);
        return true;
    }

    // Consumidor (Inference Thread): Wait-Free O(1)
    [[nodiscard]] inline bool pop(T* __restrict dst, size_t count) noexcept {
        const size_t currentTail = m_tail.load(std::memory_order_relaxed);
        const size_t currentHead = m_head.load(std::memory_order_acquire);

        if (currentHead - currentTail < count) {
            return false; // No hay suficientes muestras listas
        }

        const size_t mask = Capacity - 1;
        const size_t idx  = currentTail & mask;

        if (idx + count <= Capacity) [[likely]] {
            std::memcpy(dst, &m_storage[idx], count * sizeof(T));
        } else {
            const size_t firstPart = Capacity - idx;
            std::memcpy(dst, &m_storage[idx], firstPart * sizeof(T));
            std::memcpy(dst + firstPart, &m_storage[0], (count - firstPart) * sizeof(T));
        }

        m_tail.store(currentTail + count, std::memory_order_release);
        return true;
    }

    [[nodiscard]] inline size_t available() const noexcept {
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        return head - tail;
    }

private:
    alignas(IVANNA_CACHE_LINE) T m_storage[Capacity];
    alignas(IVANNA_CACHE_LINE) std::atomic<size_t> m_head;
    alignas(IVANNA_CACHE_LINE) std::atomic<size_t> m_tail;
};

// ============================================================================
// IvannaTinyMLAudioKernel: Motor Causal de Inteligencia Acústica
// ============================================================================
class alignas(IVANNA_CACHE_LINE) IvannaTinyMLAudioKernel {
public:
    IvannaTinyMLAudioKernel() noexcept;
    ~IvannaTinyMLAudioKernel() noexcept;

    IvannaTinyMLAudioKernel(const IvannaTinyMLAudioKernel&) = delete;
    IvannaTinyMLAudioKernel& operator=(const IvannaTinyMLAudioKernel&) = delete;
    IvannaTinyMLAudioKernel(IvannaTinyMLAudioKernel&&) = delete;
    IvannaTinyMLAudioKernel& operator=(IvannaTinyMLAudioKernel&&) = delete;

    /**
     * @brief Ingesta desde el Callback Real-Time de Audio.
     *        Zero-alloc, ejecución no bloqueante (< 3 us para bloques de 128 muestras).
     */
    void ingestPcmBlock(const float* __restrict left,
                        const float* __restrict right,
                        size_t numFrames) noexcept;

    /**
     * @brief Lectura de control acústico instantáneo para el hilo DSP.
     *        Wait-Free Triple Buffering Exchange. Latencia determinista < 5 ns.
     */
    void getAcousticControl(AcousticControlVector& outControl) const noexcept;

    /**
     * @brief Carga y verificación de pesos INT8 cuantizados en caliente.
     */
    bool loadTrainedModelWeights(std::span<const uint8_t> binaryBlob) noexcept;

private:
    void workerInferenceLoop() noexcept;
    void computeCausalMelSpectrogram(const float* __restrict inputFrame) noexcept;
    void executeInt8StreamingInference() noexcept;
    void initializeFilterbanks() noexcept;

    SpscAudioRingBuffer<float, kRingCapacity> m_ringBuffer;

    IVANNA_ALIGNED_SIMD float m_decimRing[8]{0.0f};
    size_t m_decimRingPos{0};
    size_t m_decimPhaseCounter{0};

    IVANNA_ALIGNED_SIMD float m_analysisWindow[kFftWindowSize]{0.0f};
    IVANNA_ALIGNED_SIMD float m_stftTimeBuffer[kFftWindowSize]{0.0f};
    IVANNA_ALIGNED_SIMD float m_powerSpectrum[kFftSpectrumBins]{0.0f};
    IVANNA_ALIGNED_SIMD float m_logMelFeatures[kMelFilterBands]{0.0f};
    IVANNA_ALIGNED_SIMD float m_melFilterbank[kMelFilterBands][kFftSpectrumBins]{};

    IVANNA_ALIGNED_SIMD int8_t m_gruHiddenState[kHiddenStates]{0};

    alignas(16) int8_t m_weightsConv[kMelFilterBands * 3 * 16]{0};
    alignas(16) int8_t m_weightsGru[kHiddenStates * (16 + kHiddenStates) * 3]{0};
    alignas(16) int8_t m_weightsDense[kHiddenStates * 6]{0};
    alignas(16) int8_t m_weightsRegression[kHiddenStates * 5]{0};
    std::atomic<bool>  m_isModelLoaded{false};

    mutable AcousticControlVector m_poolSlots[3];
    alignas(IVANNA_CACHE_LINE) mutable std::atomic<AcousticControlVector*> m_cleanSlot;
    alignas(IVANNA_CACHE_LINE) mutable AcousticControlVector* m_readingSlot;
    alignas(IVANNA_CACHE_LINE) AcousticControlVector* m_writingSlot;

    std::atomic<bool> m_workerRunning{false};
    std::thread       m_inferenceWorker;
};

} // namespace ivanna::tinyml
