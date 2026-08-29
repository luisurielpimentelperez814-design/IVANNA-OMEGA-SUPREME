#pragma once
/**
 * IvannaAudioClassifier.hpp — Clasificador de audio nativo (Ruta B / daemon)
 * ============================================================================
 * TCN + SE-Block + Dense con inferencia asíncrona lock-free.
 * Cuando no hay pesos cargados: clasificador heurístico espectral calibrado.
 * Ingesta PCM 48kHz estéreo → decima ×3 a 16kHz mono → FFT 512 → Mel 64
 * → clasificación con EMA temporal τ=150ms.
 */

#include "IvannaFusionCore.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <thread>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ALIGN_NEON alignas(16)
#else
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {

// ── Hiperparámetros ───────────────────────────────────────────────────────────
constexpr size_t MEL_BANDS           = 64;
constexpr size_t CLASSIFIER_FRAME_SIZE = 512;
constexpr size_t FFT_SPECTRUM_SIZE   = CLASSIFIER_FRAME_SIZE / 2 + 1;  // 257
constexpr size_t TINYML_CHANNELS     = 32;
constexpr size_t TINYML_SE_CHANNELS  = 8;
constexpr size_t NUM_CLASSES         = 4;  // 0=Speech, 1=Music, 2=Transient, 3=Noise
constexpr size_t RING_BUFFER_CAPACITY = 16384;
constexpr float  PI_F = 3.14159265358979323846f;

// ── SPSC Ring Buffer lock-free ────────────────────────────────────────────────
template <typename T, size_t Capacity>
class alignas(64) LockFreeAudioRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power-of-two");
public:
    LockFreeAudioRingBuffer() : m_head(0), m_tail(0) {}

    inline bool push(const T* src, size_t count) noexcept {
        const size_t h = m_head.load(std::memory_order_relaxed);
        const size_t t = m_tail.load(std::memory_order_acquire);
        if (h + count - t > Capacity) return false;
        for (size_t i = 0; i < count; ++i)
            m_buffer[(h + i) & (Capacity - 1)] = src[i];
        m_head.store(h + count, std::memory_order_release);
        return true;
    }

    inline bool pop(T* dst, size_t count) noexcept {
        const size_t t = m_tail.load(std::memory_order_relaxed);
        const size_t h = m_head.load(std::memory_order_acquire);
        if (h - t < count) return false;
        for (size_t i = 0; i < count; ++i)
            dst[i] = m_buffer[(t + i) & (Capacity - 1)];
        m_tail.store(t + count, std::memory_order_release);
        return true;
    }

    inline size_t available() const noexcept {
        return m_head.load(std::memory_order_acquire)
             - m_tail.load(std::memory_order_relaxed);
    }

private:
    T m_buffer[Capacity];
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

// ── Clasificador principal ────────────────────────────────────────────────────
class alignas(64) IvannaAudioClassifier {
public:
    IvannaAudioClassifier();
    ~IvannaAudioClassifier();

    // Interfaz de ingesta (hilo de audio — hot path)
    void ingestAudioFrame(const float* left, const float* right, size_t n) noexcept;

    // Resultados (cualquier hilo — atomic)
    void    getClassProbabilities(float* outProbs) const noexcept;
    uint8_t getDominantClass() const noexcept {
        return m_dominantClass.load(std::memory_order_acquire);
    }

    // Carga de pesos externos (desde binario en assets)
    // Si no se llama, el clasificador usa el path heurístico.
    bool loadWeights(const void* data, size_t bytes) noexcept;

private:
    // ── Ring buffer y buffers de trabajo ─────────────────────────────────────
    LockFreeAudioRingBuffer<float, RING_BUFFER_CAPACITY> m_audioRingBuffer;

    ALIGN_NEON float m_frameBuffer   [CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_windowedFrame [CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_powerSpectrum [FFT_SPECTRUM_SIZE];
    ALIGN_NEON float m_melLogEnergies[MEL_BANDS];

    // ── Tablas precomputadas ──────────────────────────────────────────────────
    ALIGN_NEON float    m_hanningWindow  [CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float    m_fftTwiddleReal [CLASSIFIER_FRAME_SIZE / 2];
    ALIGN_NEON float    m_fftTwiddleImag [CLASSIFIER_FRAME_SIZE / 2];
    uint16_t            m_bitRevTable    [CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float    m_melFilterbank  [MEL_BANDS][FFT_SPECTRUM_SIZE];

    // ── Decimador 48kHz→16kHz ─────────────────────────────────────────────────
    float  m_decimFirBuf[8];    // anillo FIR anti-aliasing 5-tap
    size_t m_decimFirPos;
    size_t m_decimBufPos;       // contador de muestras de entrada (mod 3)

    // ── Pesos TCN + SE + Dense ────────────────────────────────────────────────
    ALIGN_NEON float m_tcnConvWeights   [TINYML_CHANNELS][MEL_BANDS];
    ALIGN_NEON float m_tcnConvBiases    [TINYML_CHANNELS];
    ALIGN_NEON float m_seSqueezeWeights [TINYML_SE_CHANNELS][TINYML_CHANNELS];
    ALIGN_NEON float m_seSqueezeBiases  [TINYML_SE_CHANNELS];
    ALIGN_NEON float m_seExciteWeights  [TINYML_CHANNELS][TINYML_SE_CHANNELS];
    ALIGN_NEON float m_seExciteBiases   [TINYML_CHANNELS];
    ALIGN_NEON float m_denseWeights     [NUM_CLASSES][TINYML_CHANNELS];
    ALIGN_NEON float m_denseBiases      [NUM_CLASSES];
    bool             m_weightsLoaded;

    // ── EMA temporal y estado de onset ────────────────────────────────────────
    float m_probEma  [NUM_CLASSES];   // probabilidades suavizadas
    float m_onsetPrev[MEL_BANDS];     // frame anterior para cálculo de delta

    // ── Salida atómica ────────────────────────────────────────────────────────
    std::atomic<float>   m_probabilities[NUM_CLASSES];
    std::atomic<uint8_t> m_dominantClass{0};

    // ── Hilo de inferencia ────────────────────────────────────────────────────
    std::atomic<bool> m_running{false};
    std::thread       m_inferenceThread;

    // ── Métodos internos ──────────────────────────────────────────────────────
    void initFilterbankAndWindow() noexcept;
    void inferenceLoop()           noexcept;
    void processInference()        noexcept;
    void computeSTFT(const float* frame) noexcept;
    void extractLogMelFilterbank() noexcept;
    inline void applySqueezeAndExcitation(float* feat) noexcept;
};

} // namespace Ivanna
