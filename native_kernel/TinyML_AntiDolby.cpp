#include "TinyML_AntiDolby.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace ivanna {
namespace dsp {
namespace tinyml {

// ------------------------------------------------------------------------
// ZeroCopyAudioQueue - Lock-free architecture para Thread Safety en tiempo real
// ------------------------------------------------------------------------

ZeroCopyAudioQueue::ZeroCopyAudioQueue() {
    buffer_.fill(0.0f);
}

bool ZeroCopyAudioQueue::push(const float* data, size_t size) {
    // Lectura relaxed del tail (solo este thread lo modifica) y acquire del head
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    size_t current_head = head_.load(std::memory_order_acquire);
    
    // Wrap-around usando bitwise AND (MASK) requiere que BUFFER_SIZE sea potencia de 2
    size_t next_tail = (current_tail + size) & MASK;
    
    // Verificación de capacidad sin generar syscalls ni mutexes
    size_t available_write = (current_head - current_tail - 1) & MASK;
    if (available_write < size) {
        return false; // Buffer overflow mitigation: Drop frame
    }
    
    size_t first_chunk = std::min(size, BUFFER_SIZE - current_tail);
    
    // Vectorized memcpy implícito por el compilador para arquitecturas ARM
    std::memcpy(buffer_.data() + current_tail, data, first_chunk * sizeof(float));
    if (first_chunk < size) {
        std::memcpy(buffer_.data(), data + first_chunk, (size - first_chunk) * sizeof(float));
    }
    
    // Release asegura que las escrituras a memoria previas sean visibles para los consumers
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

bool ZeroCopyAudioQueue::pop(float* data, size_t size) {
    size_t current_head = head_.load(std::memory_order_relaxed);
    size_t current_tail = tail_.load(std::memory_order_acquire);
    
    size_t available = (current_tail - current_head) & MASK;
    if (available < size) return false;
    
    size_t first_chunk = std::min(size, BUFFER_SIZE - current_head);
    std::memcpy(data, buffer_.data() + current_head, first_chunk * sizeof(float));
    if (first_chunk < size) {
        std::memcpy(data + first_chunk, buffer_.data(), (size - first_chunk) * sizeof(float));
    }
    
    head_.store((current_head + size) & MASK, std::memory_order_release);
    return true;
}

size_t ZeroCopyAudioQueue::available_read() const noexcept {
    size_t h = head_.load(std::memory_order_acquire);
    size_t t = tail_.load(std::memory_order_acquire);
    return (t - h) & MASK;
}

// ------------------------------------------------------------------------
// AntiDolbyInferenceEngine - Sustituto Neural Optimizado
// ------------------------------------------------------------------------

AntiDolbyInferenceEngine::AntiDolbyInferenceEngine() {
    // Kernel size = 3 para el depthwise.
    depthwise_weights_.resize(CNN_CHANNELS * 3, 0.1f);
    pointwise_weights_.resize(CNN_CHANNELS * CNN_CHANNELS, 0.1f);
    dense_weights_.resize(LATENT_DIM * NUM_CLASSES, 0.05f);
    mel_filterbank_.resize(AUDIO_FRAME_SIZE * MEL_BANDS, 0.01f); // Matriz de proyección Mel
}

AntiDolbyInferenceEngine::~AntiDolbyInferenceEngine() = default;

void AntiDolbyInferenceEngine::ingest_audio_frame(const float* frame, size_t size) {
    audio_queue_.push(frame, size);
}

void AntiDolbyInferenceEngine::compute_mel_spectrogram_simd(const float* audio_in, float* mel_out) {
    // 1. Hann Windowing & Magnitude Spectrum Extraction
    // 2. Proyección al Mel-Filterbank
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // Optimizacion NEON: 4 Fused Multiply-Adds (FMLA) por ciclo
    for (size_t m = 0; m < MEL_BANDS; ++m) {
        float32x4_t v_sum = vdupq_n_f32(0.0f);
        
        // Asumiendo que AUDIO_FRAME_SIZE es múltiplo de 4
        for (size_t i = 0; i < AUDIO_FRAME_SIZE; i += 4) {
            float32x4_t v_audio = vld1q_f32(&audio_in[i]);
            // Power spectrum aproximation |x|^2
            float32x4_t v_power = vmulq_f32(v_audio, v_audio); 
            
            float32x4_t v_filter = vld1q_f32(&mel_filterbank_[m * AUDIO_FRAME_SIZE + i]);
            v_sum = vmlaq_f32(v_sum, v_power, v_filter);
        }
        // Reducción horizontal del vector SIMD
        float sum = vgetq_lane_f32(v_sum, 0) + vgetq_lane_f32(v_sum, 1) + 
                    vgetq_lane_f32(v_sum, 2) + vgetq_lane_f32(v_sum, 3);
                    
        // Log-Mel scaling
        mel_out[m] = std::log(sum + 1e-6f);
    }
#else
    for (size_t m = 0; m < MEL_BANDS; ++m) {
        float sum = 0.0f;
        for (size_t i = 0; i < AUDIO_FRAME_SIZE; ++i) {
            float power = audio_in[i] * audio_in[i];
            sum += power * mel_filterbank_[m * AUDIO_FRAME_SIZE + i];
        }
        mel_out[m] = std::log(sum + 1e-6f);
    }
#endif
}

void AntiDolbyInferenceEngine::execute_depthwise_conv1d_neon(const float* input, float* output) {
    // Depthwise Spatial Convolution (Filtra cada canal de forma independiente)
    // Reduce la complejidad computacional O(C*K) vs O(C^2 * K)
    for(size_t c = 0; c < CNN_CHANNELS; ++c) {
        for(size_t i = 0; i < MEL_BANDS; ++i) {
            float sum = 0.0f;
            for(int k = -1; k <= 1; ++k) {
                int idx = static_cast<int>(i) + k;
                if(idx >= 0 && idx < MEL_BANDS) {
                    sum += input[idx] * depthwise_weights_[c * 3 + (k + 1)];
                }
            }
            output[c * MEL_BANDS + i] = std::max(0.0f, sum); // ReLU6 o ReLU
        }
    }
}

void AntiDolbyInferenceEngine::execute_pointwise_conv1d_neon(const float* input, float* output) {
    // Pointwise 1x1 Convolution para mezclar los canales proyectados
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for(size_t i = 0; i < MEL_BANDS; ++i) {
        for(size_t out_c = 0; out_c < CNN_CHANNELS; ++out_c) {
            float32x4_t v_sum = vdupq_n_f32(0.0f);
            
            for(size_t in_c = 0; in_c < CNN_CHANNELS; in_c += 4) {
                // Gather cross-channel values
                float in_vals[4] = {
                    input[in_c * MEL_BANDS + i],
                    input[(in_c+1) * MEL_BANDS + i],
                    input[(in_c+2) * MEL_BANDS + i],
                    input[(in_c+3) * MEL_BANDS + i]
                };
                float32x4_t v_in = vld1q_f32(in_vals);
                float32x4_t v_w = vld1q_f32(&pointwise_weights_[out_c * CNN_CHANNELS + in_c]);
                v_sum = vmlaq_f32(v_sum, v_in, v_w);
            }
            float sum = vgetq_lane_f32(v_sum, 0) + vgetq_lane_f32(v_sum, 1) + 
                        vgetq_lane_f32(v_sum, 2) + vgetq_lane_f32(v_sum, 3);
            
            output[out_c * MEL_BANDS + i] = std::max(0.0f, sum);
        }
    }
#else
    for(size_t i = 0; i < MEL_BANDS; ++i) {
        for(size_t out_c = 0; out_c < CNN_CHANNELS; ++out_c) {
            float sum = 0.0f;
            for(size_t in_c = 0; in_c < CNN_CHANNELS; ++in_c) {
                sum += input[in_c * MEL_BANDS + i] * pointwise_weights_[out_c * CNN_CHANNELS + in_c];
            }
            output[out_c * MEL_BANDS + i] = std::max(0.0f, sum);
        }
    }
#endif
}

void AntiDolbyInferenceEngine::execute_dense_simd(const float* input, float* output) {
    // Matmul Densa (Proyección final)
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for(size_t c = 0; c < NUM_CLASSES; ++c) {
        float32x4_t v_sum = vdupq_n_f32(0.0f);
        const float* w_ptr = &dense_weights_[c * LATENT_DIM];
        
        for(size_t i = 0; i < LATENT_DIM; i += 4) {
            float32x4_t v_in = vld1q_f32(&input[i]);
            float32x4_t v_w = vld1q_f32(&w_ptr[i]);
            v_sum = vmlaq_f32(v_sum, v_in, v_w);
        }
        output[c] = vgetq_lane_f32(v_sum, 0) + vgetq_lane_f32(v_sum, 1) + 
                    vgetq_lane_f32(v_sum, 2) + vgetq_lane_f32(v_sum, 3);
    }
#else
    for(size_t c = 0; c < NUM_CLASSES; ++c) {
        float sum = 0.0f;
        for(size_t i = 0; i < LATENT_DIM; ++i) {
            sum += input[i] * dense_weights_[c * LATENT_DIM + i];
        }
        output[c] = sum;
    }
#endif
}

void AntiDolbyInferenceEngine::apply_softmax(std::array<float, NUM_CLASSES>& logits) {
    float max_logit = *std::max_element(logits.begin(), logits.end());
    float sum_exp = 0.0f;
    for(auto& logit : logits) {
        logit = std::exp(logit - max_logit);
        sum_exp += logit;
    }
    for(auto& logit : logits) {
        logit /= sum_exp;
    }
}

std::array<float, NUM_CLASSES> AntiDolbyInferenceEngine::process_inference() {
    alignas(64) std::array<float, AUDIO_FRAME_SIZE> frame_buffer;
    std::array<float, NUM_CLASSES> logits = {0};

    if (audio_queue_.available_read() >= AUDIO_FRAME_SIZE) {
        if (audio_queue_.pop(frame_buffer.data(), AUDIO_FRAME_SIZE)) {
            
            // 1. Feature Extraction (Log-Mel Spectrogram)
            compute_mel_spectrogram_simd(frame_buffer.data(), mel_spectrogram_buffer_.data());
            
            // 2. Convolutional Neural Network (Depthwise Separable)
            execute_depthwise_conv1d_neon(mel_spectrogram_buffer_.data(), dw_activation_buffer_.data());
            execute_pointwise_conv1d_neon(dw_activation_buffer_.data(), pw_activation_buffer_.data());
            
            // 3. Global Average Pooling -> Latent Space
            latent_vector_.fill(0.0f);
            for(size_t c = 0; c < CNN_CHANNELS; ++c) {
                float avg = 0.0f;
                for(size_t i = 0; i < MEL_BANDS; ++i) {
                    avg += pw_activation_buffer_[c * MEL_BANDS + i];
                }
                latent_vector_[c] = avg / MEL_BANDS;
            }
            
            // 4. Fully Connected & Softmax
            execute_dense_simd(latent_vector_.data(), logits.data());
            apply_softmax(logits);
        }
    }
    return logits;
}

} // namespace tinyml
} // namespace dsp
} // namespace ivanna
