#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>
#include <immintrin.h>

namespace ivanna {
namespace dsp {
namespace tinyml {

// Constantes de arquitectura para el clasificador acústico
constexpr size_t AUDIO_FRAME_SIZE = 1024;
constexpr size_t MEL_BANDS = 64;
constexpr size_t CNN_FILTERS = 32;
constexpr size_t LATENT_DIM = 128;
constexpr size_t NUM_CLASSES = 10; // Ejemplo: Voz, Ruido, Música, Dolby_Artifact, etc.

/**
 * @brief Lock-free ring buffer para ingesta de audio de ultra baja latencia.
 * Evita bloqueos en el hilo de audio de alta prioridad (SCHED_FIFO).
 */
class LockFreeAudioQueue {
public:
    LockFreeAudioQueue();
    ~LockFreeAudioQueue() = default;

    bool push(const float* data, size_t size);
    bool pop(float* data, size_t size);
    
    size_t available_read() const;

private:
    static constexpr size_t BUFFER_SIZE = 8192;
    alignas(64) std::array<float, BUFFER_SIZE> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

/**
 * @brief Motor de inferencia nativo altamente optimizado con SIMD (NEON/AVX).
 * Reemplaza a YAMNet para clasificación acústica y supresión de artefactos Dolby.
 */
class AntiDolbyInferenceEngine {
public:
    AntiDolbyInferenceEngine();
    ~AntiDolbyInferenceEngine();

    // Deshabilita copias para evitar degradación de rendimiento
    AntiDolbyInferenceEngine(const AntiDolbyInferenceEngine&) = delete;
    AntiDolbyInferenceEngine& operator=(const AntiDolbyInferenceEngine&) = delete;

    /**
     * @brief Ingesta asíncrona de frames de audio.
     * Retorna inmediatamente, diseñado para el callback del daemon.
     */
    void ingest_audio_frame(const float* frame, size_t size);

    /**
     * @brief Procesa la inferencia en un hilo worker.
     * @return std::array con las probabilidades de clase.
     */
    std::array<float, NUM_CLASSES> process_inference();

private:
    LockFreeAudioQueue audio_queue_;

    // Pesos del modelo TinyML, alineados en memoria para operaciones SIMD.
    // En producción, esto se mapearía usando mmap desde un archivo FLATBUFFER/TFLite.
    alignas(64) std::vector<float> cnn_weights_;
    alignas(64) std::vector<float> dense_weights_;

    // Buffers intermedios (Evita alocaciones dinámicas durante inferencia)
    alignas(64) std::array<float, MEL_BANDS> mel_spectrogram_buffer_;
    alignas(64) std::array<float, CNN_FILTERS * MEL_BANDS> conv_activation_buffer_;
    
    // Extracción de características (MFCC / Mel-Spectrogram) usando SIMD
    void compute_mel_spectrogram(const float* audio_in, float* mel_out);
    
    // Capas de inferencia con intrinsics
    void execute_conv1d_simd(const float* input, float* output);
    void execute_dense_simd(const float* input, float* output);
};

} // namespace tinyml
} // namespace dsp
} // namespace ivanna
