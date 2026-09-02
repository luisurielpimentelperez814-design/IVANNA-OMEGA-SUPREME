#pragma once

#include "TinyML_AntiDolby.hpp"
#include <pthread.h>
#include <atomic>
#include <memory>
#include <vector>

namespace ivanna {
namespace dsp {
namespace daemon {

/**
 * @brief Daemon interceptor a nivel de HAL (Hardware Abstraction Layer).
 * Diseñado para ser inyectado vía Magisk. Se encarga de enganchar los buffers
 * de audio antes del mixer de Android, evaluarlos con el TinyML y aplicar 
 * supresión de fase/amplitud para neutralizar los artefactos de Dolby Atmos.
 */
class AntiDolbyInterceptor {
public:
    AntiDolbyInterceptor();
    ~AntiDolbyInterceptor();

    // Prevent copying to maintain thread safety and singleton-like behavior in the hook
    AntiDolbyInterceptor(const AntiDolbyInterceptor&) = delete;
    AntiDolbyInterceptor& operator=(const AntiDolbyInterceptor&) = delete;

    /**
     * @brief Inicializa el hilo worker de alta prioridad (SCHED_FIFO).
     */
    bool start_daemon();

    /**
     * @brief Detiene el hilo de inferencia de manera segura.
     */
    void stop_daemon();

    /**
     * @brief Hot-path. Callback de audio inyectado. 
     * @param in_out_buffer Buffer entrelazado (L/R) a procesar in-place.
     * @param num_frames Número de frames (muestras por canal).
     * @param channels Número de canales (usualmente 2).
     */
    void process_audio_buffer(float* in_out_buffer, size_t num_frames, int channels);

private:
    std::unique_ptr<tinyml::AntiDolbyInferenceEngine> ml_engine_;

    // Concurrencia
    pthread_t inference_thread_;
    std::atomic<bool> is_running_{false};

    // Estado del supresor (Lock-free state update)
    std::atomic<float> suppression_coefficient_{0.0f};

    // Función de hilo POSIX
    static void* inference_worker_routine(void* context);
    void run_inference_loop();

    // DSP: Corrección de fase y ecualización dinámica SIMD
    void apply_inverse_dolby_dsp(float* buffer, size_t num_samples, float suppression_level);
};

} // namespace daemon
} // namespace dsp
} // namespace ivanna
