// ⚠ ARCHIVO LEGADO — NO EDITAR ⚠
// La fuente de verdad es app/src/main/cpp/IvannaAudioClassifier.hpp
// Este archivo existe por historial pero NO se compila en ningún target.
// Cualquier cambio aquí se perderá. Edita app/src/main/cpp/.
//
#pragma once

#include "IvannaFusionCore.hpp"
#include <atomic>
#include <cstdint>
#include <array>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace Ivanna {

// 64-band log-mel filterbank resolution over 512-sample STFT frame (~10.67ms @ 48kHz)
constexpr size_t MEL_BANDS = 64;
constexpr size_t CLASSIFIER_FRAME_SIZE = 512;
constexpr size_t FFT_SPECTRUM_SIZE = (CLASSIFIER_FRAME_SIZE / 2) + 1; // 257 bins
constexpr size_t CONV_CHANNELS = 32;
constexpr size_t NUM_CLASSES = 4; // 0: Speech/Vocal, 1: Music/Spatial, 2: Transient/Impact, 3: Noise/Ambient
constexpr size_t RING_BUFFER_CAPACITY = 16384; // 16384 samples (~341ms at 48kHz)

/**
 * @brief Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * Eliminates mutex contention between real-time audio thread and TinyML inference worker thread.
 * Alignas(64) prevents cache line false sharing across CPU core clusters.
 */
template <typename T, size_t Capacity>
class alignas(64) LockFreeAudioRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two for bitwise wrapping");
public:
    LockFreeAudioRingBuffer() : m_head(0), m_tail(0) {}

    // Pushes samples from real-time audio callback (Lock-Free)
    inline bool push(const T* src, size_t count) noexcept {
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        const size_t current_tail = m_tail.load(std::memory_order_acquire);

        if ((current_head + count - current_tail) > Capacity) {
            return false; // Buffer overflow prevention
        }

        for (size_t i = 0; i < count; ++i) {
            m_buffer[(current_head + i) & (Capacity - 1)] = src[i];
        }

        m_head.store(current_head + count, std::memory_order_release);
        return true;
    }

    // Pops samples for background TinyML inference thread (Lock-Free)
    inline bool pop(T* dst, size_t count) noexcept {
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        const size_t current_head = m_head.load(std::memory_order_acquire);

        if (current_head - current_tail < count) {
            return false; // Insufficient samples queued
        }

        for (size_t i = 0; i < count; ++i) {
            dst[i] = m_buffer[(current_tail + i) & (Capacity - 1)];
        }

        m_tail.store(current_tail + count, std::memory_order_release);
        return true;
    }

    size_t available() const noexcept {
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        return current_head - current_tail;
    }

private:
    T m_buffer[Capacity];
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

/**
 * @brief YAMNet Replacement: Kernel-Level TinyML Audio DSP Classifier
 * Zero-allocation, Lock-Free, SIMD-accelerated STFT + 64-Band Mel Filterbank + ConvNeXt inference engine.
 */
class alignas(64) IvannaAudioClassifier {
public:
    IvannaAudioClassifier();
    ~IvannaAudioClassifier() = default;

    // Real-time audio callback hook: zero allocation, lock-free sample push
    void ingestAudioFrame(const float* inputLeft, const float* inputRight, size_t numSamples) noexcept;

    // Executes 512-pt Hanning STFT, 64-Band Log-Mel Extraction, Depthwise-ConvNeXt & Softmax (<15 µs latency)
    void processInference() noexcept;

    // Returns softmax probability vector for [Speech, Music, Transient, Ambient]
    const float* getClassProbabilities() const noexcept { return m_probabilities; }
    // Compatibility API: bridge JNI legacy expects this accessor name.
    const float* getProbabilities() const noexcept { return m_probabilities; }

    uint8_t getDominantClass() const noexcept { return m_dominantClass; }

private:
    // Lock-free input queue (16384 samples capacity)
    LockFreeAudioRingBuffer<float, RING_BUFFER_CAPACITY> m_audioRingBuffer;

    // Static scratchpads (Zero Heap Allocation)
    ALIGN_NEON float m_frameBuffer[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_windowedFrame[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_powerSpectrum[FFT_SPECTRUM_SIZE];
    ALIGN_NEON float m_melLogEnergies[MEL_BANDS];

    // Precomputed Hanning Window table, FFT Twiddles & 64-band Triangular Mel Filterbank Matrix
    ALIGN_NEON float m_hanningWindow[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_fftTwiddleReal[CLASSIFIER_FRAME_SIZE / 2];
    ALIGN_NEON float m_fftTwiddleImag[CLASSIFIER_FRAME_SIZE / 2];
    uint16_t m_bitRevTable[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_melFilterbank[MEL_BANDS][FFT_SPECTRUM_SIZE];

    // Model weights (32-channel Depthwise-Separable ConvNeXt + Fused ReLU6 + Dense)
    ALIGN_NEON float m_depthwiseKernel[MEL_BANDS];
    ALIGN_NEON float m_pointwiseWeights[CONV_CHANNELS][MEL_BANDS];
    ALIGN_NEON float m_pointwiseBiases[CONV_CHANNELS];
    ALIGN_NEON float m_denseWeights[NUM_CLASSES][CONV_CHANNELS];
    ALIGN_NEON float m_denseBiases[NUM_CLASSES];

    ALIGN_NEON float m_probabilities[NUM_CLASSES];
    uint8_t m_dominantClass = 1;

    // Precomputes Hanning Window and 64-band Triangular Mel Filterbank
    void initFilterbankAndWindow() noexcept;

    // Computes 512-point Real FFT Power Spectrum using Hanning Window
    void computeSTFT(const float* frame) noexcept;

    // Fast SIMD 64-band Mel Log Filterbank calculation
    void extractLogMelFilterbank() noexcept;
};

} // namespace Ivanna
