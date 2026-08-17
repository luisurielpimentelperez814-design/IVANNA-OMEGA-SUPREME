sed -i 's/#include <cmath>/#include <cmath>\n#include <thread>\n#include <chrono>\n#include <cstring>/g' /app/applet/app/src/main/cpp/IvannaAudioClassifier.hpp

# Update IvannaAudioClassifier.hpp
cat << 'HPP_EOF' > /app/applet/app/src/main/cpp/IvannaAudioClassifier.hpp
// ─────────────────────────────────────────────────────────────────────────────
// IVANNA OMEGA SUPREME - KERNEL-LEVEL TINYML AUDIO CLASSIFIER
// ─────────────────────────────────────────────────────────────────────────────
// Architect: Principal Audio DSP & Kernel TinyML Specialist
// Design: Temporal Convolutional Network (TCN) with Squeeze-and-Excitation (SE)
// Execution: Lock-free, NEON-accelerated, Zero frame-loss.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "IvannaFusionCore.hpp"
#include <atomic>
#include <cstdint>
#include <array>
#include <cmath>
#include <thread>
#include <chrono>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {

#ifndef SAMPLE_RATE
constexpr float SAMPLE_RATE = 48000.0f;
#endif

#ifndef SAMPLING_RATE
constexpr float SAMPLING_RATE = SAMPLE_RATE;
#endif

// ── Model Architecture Hyperparameters ────────────────────────
constexpr size_t MEL_BANDS = 64;
constexpr size_t CLASSIFIER_FRAME_SIZE = 512;
constexpr size_t FFT_SPECTRUM_SIZE = (CLASSIFIER_FRAME_SIZE / 2) + 1; // 257 bins
constexpr size_t TINYML_CHANNELS = 32;
constexpr size_t TINYML_SE_CHANNELS = 8; // Squeeze ratio = 4
constexpr size_t NUM_CLASSES = 4; // 0: Speech/Vocal, 1: Music/Spatial, 2: Transient/Impact, 3: Noise/Ambient

// Lock-free configuration
constexpr size_t RING_BUFFER_CAPACITY = 16384; 
constexpr float PI_F = 3.14159265358979323846f;

// ── Lock-Free Single-Producer Single-Consumer Ring Buffer ─────
// Garantiza la supremacía acústica sin locks ni bloqueos (zero frame-loss)
template <typename T, size_t Capacity>
class alignas(64) LockFreeAudioRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two for bitwise mask wrapping");
public:
    LockFreeAudioRingBuffer() : m_head(0), m_tail(0) {}

    // Atomic push ensures deterministic memory ordering (release semantic)
    inline bool push(const T* src, size_t count) noexcept {
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        const size_t current_tail = m_tail.load(std::memory_order_acquire);
        
        // Prevent overflow, hard constraints on audio pipeline real-time boundaries
        if ((current_head + count - current_tail) > Capacity) {
            return false;
        }
        
        for (size_t i = 0; i < count; ++i) {
            m_buffer[(current_head + i) & (Capacity - 1)] = src[i];
        }
        
        m_head.store(current_head + count, std::memory_order_release);
        return true;
    }

    // Atomic pop prevents dirty reads via acquire semantic synchronization
    inline bool pop(T* dst, size_t count) noexcept {
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        const size_t current_head = m_head.load(std::memory_order_acquire);
        
        if (current_head - current_tail < count) {
            return false;
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
    // Cacheline isolation (64-byte padding) mitigates false sharing en arquitecturas SMP
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

// ── Kernel-level TinyML Classifier ────────────────────────────
class alignas(64) IvannaAudioClassifier {
public:
    IvannaAudioClassifier();
    ~IvannaAudioClassifier();

    // Real-time PCM ingestion (Hot-path DSP daemon)
    void ingestAudioFrame(const float* inputLeft, const float* inputRight, size_t numSamples) noexcept;

    // Return cached probabilities safely using atomics (zero-locks)
    const float* getProbabilities() noexcept;
    uint8_t getDominantClass() const noexcept { return m_atomicDominant.load(std::memory_order_acquire); }

private:
    LockFreeAudioRingBuffer<float, RING_BUFFER_CAPACITY> m_audioRingBuffer;
    
    // Async Inference Thread
    std::atomic<bool> m_running;
    std::thread m_inferenceThread;
    void inferenceLoop() noexcept;
    void processInference() noexcept;
    
    // Pre-allocated ALIGN_NEON buffers to dodge page faults and allocator locks
    ALIGN_NEON float m_frameBuffer[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_windowedFrame[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_powerSpectrum[FFT_SPECTRUM_SIZE];
    ALIGN_NEON float m_melLogEnergies[MEL_BANDS];
    
    // DSP Pre-computation tables
    ALIGN_NEON float m_hanningWindow[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_fftTwiddleReal[CLASSIFIER_FRAME_SIZE / 2];
    ALIGN_NEON float m_fftTwiddleImag[CLASSIFIER_FRAME_SIZE / 2];
    uint16_t m_bitRevTable[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_melFilterbank[MEL_BANDS][FFT_SPECTRUM_SIZE];

    // TinyML Architectural Weights (SE-TCN)
    ALIGN_NEON float m_tcnConvWeights[TINYML_CHANNELS][MEL_BANDS];
    ALIGN_NEON float m_tcnConvBiases[TINYML_CHANNELS];
    
    // Squeeze-and-Excitation block weights (Contextual Channel Recalibration)
    ALIGN_NEON float m_seSqueezeWeights[TINYML_SE_CHANNELS][TINYML_CHANNELS];
    ALIGN_NEON float m_seSqueezeBiases[TINYML_SE_CHANNELS];
    ALIGN_NEON float m_seExciteWeights[TINYML_CHANNELS][TINYML_SE_CHANNELS];
    ALIGN_NEON float m_seExciteBiases[TINYML_CHANNELS];

    ALIGN_NEON float m_denseWeights[NUM_CLASSES][TINYML_CHANNELS];
    ALIGN_NEON float m_denseBiases[NUM_CLASSES];
    
    // Output state (Atomic for thread-safety without locks)
    std::atomic<uint32_t> m_atomicProbs[NUM_CLASSES];
    std::atomic<uint8_t> m_atomicDominant;
    float m_cachedReturnedProbs[NUM_CLASSES];

    // Internal pipeline phases
    void initFilterbankAndWindow() noexcept;
    void computeSTFT(const float* frame) noexcept;
    void extractLogMelFilterbank() noexcept;
    
    // Inline execution of NEON-accelerated SE block
    inline void applySqueezeAndExcitation(float* featureMap) noexcept;
};

} // namespace Ivanna
HPP_EOF
