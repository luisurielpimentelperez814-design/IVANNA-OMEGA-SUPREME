#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
#include <immintrin.h>
#endif

namespace ivanna {
namespace dsp {
namespace tinyml {

// Arquitectura de Red Neuronal Profunda para Supresión Anti-Dolby
// Optimizada para el Kernel de Audio de Android (SCHED_FIFO)
constexpr size_t AUDIO_FRAME_SIZE = 1024;
constexpr size_t MEL_BANDS = 64;
constexpr size_t CNN_CHANNELS = 32;
constexpr size_t LATENT_DIM = 128;
constexpr size_t NUM_CLASSES = 10; 
// Clases objetivo: [Speech, Music, Noise, Dolby_Artifact, Harmonic_Distortion, Phase_Tear...]

/**
 * @brief Lock-Free Zero-Copy Ring Buffer.
 * Diseñado explícitamente para eludir page faults y system calls en el
 * audio callback. Utiliza memory_order_acquire/release para visibilidad
 * cruzada entre el hilo FastMixer y el daemon thread.
 */
class ZeroCopyAudioQueue {
public:
    ZeroCopyAudioQueue();
    ~ZeroCopyAudioQueue() = default;

    // Deshabilita semántica de copia para garantizar unicidad de punteros
    ZeroCopyAudioQueue(const ZeroCopyAudioQueue&) = delete;
    ZeroCopyAudioQueue& operator=(const ZeroCopyAudioQueue&) = delete;

    /**
     * @brief Inyecta muestras en O(1) vía memcpy SIMD vectorizado.
     */
    bool push(const float* data, size_t size);

    /**
     * @brief Extrae muestras en O(1) garantizando alineación de cache-line.
     */
    bool pop(float* data, size_t size);
    
    size_t available_read() const noexcept;

private:
    static constexpr size_t BUFFER_SIZE = 16384; // Potencia de 2 para wrap-around bitwise
    static constexpr size_t MASK = BUFFER_SIZE - 1;

    // Alineación a 64 bytes para evitar False Sharing en arquitecturas SMP
    alignas(64) std::array<float, BUFFER_SIZE> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

/**
 * @brief Ivanna Neural Engine (Sustituto YAMNet).
 * Implementa Separable Depthwise Convolutions y Fast-Fourier Transforms (FFT) 
 * altamente paralelizadas mediante ARM NEON intrinsics para latencia < 1ms.
 */
class AntiDolbyInferenceEngine {
public:
    AntiDolbyInferenceEngine();
    ~AntiDolbyInferenceEngine();

    /**
     * @brief Ingesta no bloqueante desde la capa HAL.
     */
    void ingest_audio_frame(const float* frame, size_t size);

    /**
     * @brief Flujo de Inferencia.
     * 1. Extracción FFT / Log-Mel
     * 2. Depthwise Separable Conv1D
     * 3. Dense Projection
     * 4. Softmax
     * @return Logits normalizados de las anomalías acústicas.
     */
    std::array<float, NUM_CLASSES> process_inference();

private:
    ZeroCopyAudioQueue audio_queue_;

    // Pesos Quantizados (FP32 actualmente, listos para migracion FP16)
    alignas(64) std::vector<float> depthwise_weights_;
    alignas(64) std::vector<float> pointwise_weights_;
    alignas(64) std::vector<float> dense_weights_;
    alignas(64) std::vector<float> mel_filterbank_;

    // Tensors intermedios alineados
    alignas(64) std::array<float, MEL_BANDS> mel_spectrogram_buffer_;
    alignas(64) std::array<float, CNN_CHANNELS * MEL_BANDS> dw_activation_buffer_;
    alignas(64) std::array<float, CNN_CHANNELS * MEL_BANDS> pw_activation_buffer_;
    alignas(64) std::array<float, LATENT_DIM> latent_vector_;

    // Operaciones del modelo de Deep Learning (Optimizadas con vectorización explícita)
    void compute_mel_spectrogram_simd(const float* audio_in, float* mel_out);
    void execute_depthwise_conv1d_neon(const float* input, float* output);
    void execute_pointwise_conv1d_neon(const float* input, float* output);
    void execute_dense_simd(const float* input, float* output);
    void apply_softmax(std::array<float, NUM_CLASSES>& logits);
};

} // namespace tinyml
} // namespace dsp
} // namespace ivanna
