#include "AntiDolby_AudioHook.hpp"
#include <sched.h>
#include <unistd.h>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna {
namespace dsp {
namespace daemon {

AntiDolbyInterceptor::AntiDolbyInterceptor() {
    ml_engine_ = std::make_unique<tinyml::AntiDolbyInferenceEngine>();
}

AntiDolbyInterceptor::~AntiDolbyInterceptor() {
    stop_daemon();
}

bool AntiDolbyInterceptor::start_daemon() {
    if (is_running_.load(std::memory_order_acquire)) return true;
    
    is_running_.store(true, std::memory_order_release);
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    // Configurar política de planificación Real-Time para evitar preemption
    // Crítico en Android para evitar pérdida de frames de audio
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1; 
    pthread_attr_setschedparam(&attr, &param);

    int result = pthread_create(&inference_thread_, &attr, inference_worker_routine, this);
    pthread_attr_destroy(&attr);

    return result == 0;
}

void AntiDolbyInterceptor::stop_daemon() {
    if (is_running_.load(std::memory_order_acquire)) {
        is_running_.store(false, std::memory_order_release);
        pthread_join(inference_thread_, nullptr);
    }
}

void* AntiDolbyInterceptor::inference_worker_routine(void* context) {
    auto* daemon = static_cast<AntiDolbyInterceptor*>(context);
    daemon->run_inference_loop();
    return nullptr;
}

void AntiDolbyInterceptor::run_inference_loop() {
    // Loop de Inferencia Aislado
    while (is_running_.load(std::memory_order_acquire)) {
        // Ejecución TCN + SE-Block
        auto logits = ml_engine_->process_inference();
        
        // Asumimos logits[3] es la clase "Dolby_Artifact"
        float dolby_probability = logits[3];

        // Suavizado del coeficiente de supresión (Filtro IIR unipolar de 1er orden)
        float current_suppression = suppression_coefficient_.load(std::memory_order_relaxed);
        float target_suppression = (dolby_probability > 0.65f) ? dolby_probability : 0.0f;
        
        // Ataque rápido, Release lento
        float alpha = (target_suppression > current_suppression) ? 0.15f : 0.005f;
        float new_suppression = current_suppression + alpha * (target_suppression - current_suppression);
        
        suppression_coefficient_.store(new_suppression, std::memory_order_release);
        
        // Micro-sleep para evitar thrashing del CPU, scheduler yield
        usleep(1500); 
    }
}

void AntiDolbyInterceptor::apply_inverse_dolby_dsp(float* buffer, size_t num_samples, float suppression_level) {
    if (suppression_level < 0.01f) return;

    // Supresión inversa lock-free y zero-allocation con SIMD
    float attenuation_factor = 1.0f - (suppression_level * 0.45f);

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_attenuation = vdupq_n_f32(attenuation_factor);
    
    size_t i = 0;
    for (; i + 3 < num_samples; i += 4) {
        float32x4_t v_audio = vld1q_f32(&buffer[i]);
        v_audio = vmulq_f32(v_audio, v_attenuation);
        vst1q_f32(&buffer[i], v_audio);
    }
    // Tail
    for (; i < num_samples; ++i) {
        buffer[i] *= attenuation_factor;
    }
#else
    for (size_t i = 0; i < num_samples; ++i) {
        buffer[i] *= attenuation_factor;
    }
#endif
}

void AntiDolbyInterceptor::process_audio_buffer(float* in_out_buffer, size_t num_frames, int channels) {
    size_t total_samples = num_frames * channels;

    // 1. Ingesta Lock-Free al modelo TinyML
    // Downmix a mono trivial
    alignas(64) float downmix_buffer[1024]; 
    size_t ingest_size = std::min(num_frames, (size_t)1024);
    
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if (channels == 2) {
        float32x4_t v_half = vdupq_n_f32(0.5f);
        size_t i = 0;
        for (; i + 3 < ingest_size; i += 4) {
            float32x4_t v_left = { in_out_buffer[i*2], in_out_buffer[(i+1)*2], in_out_buffer[(i+2)*2], in_out_buffer[(i+3)*2] };
            float32x4_t v_right = { in_out_buffer[i*2+1], in_out_buffer[(i+1)*2+1], in_out_buffer[(i+2)*2+1], in_out_buffer[(i+3)*2+1] };
            float32x4_t v_mono = vmulq_f32(vaddq_f32(v_left, v_right), v_half);
            vst1q_f32(&downmix_buffer[i], v_mono);
        }
        for (; i < ingest_size; ++i) {
            downmix_buffer[i] = (in_out_buffer[i * 2] + in_out_buffer[i * 2 + 1]) * 0.5f;
        }
    } else {
        std::memcpy(downmix_buffer, in_out_buffer, ingest_size * sizeof(float));
    }
#else
    if (channels == 2) {
        for(size_t i = 0; i < ingest_size; ++i) {
            downmix_buffer[i] = (in_out_buffer[i * 2] + in_out_buffer[i * 2 + 1]) * 0.5f;
        }
    } else {
        std::memcpy(downmix_buffer, in_out_buffer, ingest_size * sizeof(float));
    }
#endif
    
    ml_engine_->ingest_audio_frame(downmix_buffer, ingest_size);

    // 2. Coeficiente Thread-Safe
    float suppression = suppression_coefficient_.load(std::memory_order_acquire);

    // 3. Modificador acústico
    apply_inverse_dolby_dsp(in_out_buffer, total_samples, suppression);
}

} // namespace daemon
} // namespace dsp
} // namespace ivanna
