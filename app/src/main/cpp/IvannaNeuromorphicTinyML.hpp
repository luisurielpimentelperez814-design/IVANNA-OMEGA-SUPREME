#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <cstdint>
#include <array>
#include <cstring>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna {
namespace dsp {

/**
 * @class IvannaNeuromorphicTinyML
 * @brief Zero-allocation, lock-free, SIMD-accelerated audio inference engine.
 * 
 * Replaces the legacy YAMNet model with a low-latency Depthwise Separable CNN + 
 * Spiking Neural Network (SNN) hybrid model, operating directly at the kernel
 * audio buffer level via Magisk daemon bridge.
 * 
 * Memory strategy: All buffers pre-allocated and 32-byte aligned for NEON / portable SIMD.
 * Concurrency: Wait-free ring buffers (SPSC) for feature extraction -> inference handoff.
 */
class IvannaNeuromorphicTinyML {
public:
    // Frame sizes optimized for 48kHz audio (10ms windows)
    static constexpr size_t FRAME_SIZE = 480; 
    static constexpr size_t NUM_MFCC_BINS = 40;
    static constexpr size_t EMBEDDING_SIZE = 128;
    static constexpr size_t ALIGNMENT = 32;

    IvannaNeuromorphicTinyML();
    ~IvannaNeuromorphicTinyML();

    // Prevent copies for lock-free safety
    IvannaNeuromorphicTinyML(const IvannaNeuromorphicTinyML&) = delete;
    IvannaNeuromorphicTinyML& operator=(const IvannaNeuromorphicTinyML&) = delete;

    /**
     * @brief Pushes a raw PCM float32 block (must be lock-free / wait-free).
     * Called directly from the FastMixer / AudioFlinger hook thread.
     */
    void processAudioFrame(const float* __restrict input_buffer, size_t num_samples);

    /**
     * @brief Retrieves the latest acoustic scene embedding.
     * Wait-free read using atomic seqlocks.
     */
    void getLatestEmbedding(float* __restrict out_embedding);

private:
    // Aligned memory allocation for auto-vectorization and SIMD
    struct alignas(ALIGNMENT) DSPBuffers {
        std::array<float, FRAME_SIZE> windowed_frame;
        std::array<float, NUM_MFCC_BINS> mfcc_features;
        std::array<float, EMBEDDING_SIZE> current_embedding;
    };

    std::unique_ptr<DSPBuffers> m_buffers;
    
    // Lock-free synchronization via SeqLock pattern for embedding reads
    std::atomic<uint32_t> m_seqlock{0};

    // Fast-math feature extraction (SIMD)
    void extractFeaturesNEON(const float* __restrict input, float* __restrict features);
    
    // Depthwise separable convolution block (TinyML inference)
    void executeDepthwiseConvBlock(const float* __restrict features, float* __restrict output);
};

} // namespace dsp
} // namespace ivanna
