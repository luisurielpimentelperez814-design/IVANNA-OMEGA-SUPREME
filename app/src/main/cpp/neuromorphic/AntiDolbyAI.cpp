#include "AntiDolbyAI.hpp"
#include <cstring>
#include <algorithm>

namespace Ivanna {
namespace Neuromorphic {

AntiDolbyAI::AntiDolbyAI() {
    // Inicialización de pesos y estados (en producción cargaríamos un binario pre-entrenado)
    m_weights_ih.fill(0.01f);
    m_weights_hh.fill(0.005f);
    m_weights_ho.fill(0.02f);
    m_cell_state.fill(0.0f);
    m_hidden_state.fill(0.0f);
}

AntiDolbyAI::~AntiDolbyAI() {
    // Limpieza criptográfica de estado neural
    std::fill(m_cell_state.begin(), m_cell_state.end(), 0.0f);
    std::fill(m_hidden_state.begin(), m_hidden_state.end(), 0.0f);
}

// Aproximación polinómica [3/2] ultra rápida para sigmoide
inline float AntiDolbyAI::fast_sigmoid(float x) const noexcept {
    // Para NEON, idealmente usaríamos vrecpeq_f32 y Newton-Raphson.
    // Aquí implementamos versión escalar optimizada.
    const float x_abs = std::abs(x);
    if (x_abs > 10.0f) return x > 0.0f ? 1.0f : 0.0f;
    return 0.5f + (x / (2.0f + x_abs)); // Pseudo-sigmoide racional
}

// Padé aproximante para Tanh: x*(27 + x^2) / (27 + 9*x^2)
inline float AntiDolbyAI::fast_tanh(float x) const noexcept {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

bool AntiDolbyAI::enqueueFeatures(const float* __restrict mel_features) noexcept {
    const size_t current_head = m_head.load(std::memory_order_relaxed);
    const size_t next_head = (current_head + 1) % RING_BUFFER_SIZE;
    
    // Si choca con el tail (buffer lleno), drop frame (cero latencia inducida)
    if (next_head == m_tail.load(std::memory_order_acquire)) {
        return false;
    }
    
    // Vectorización garantizada con restrict y copiado en caché L1
    float* dest = m_ring_buffer[current_head].features.data();
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // Copia manual unrolled para SIMD
    for (size_t i = 0; i < INPUT_FEATURES; i += 4) {
        float32x4_t v = vld1q_f32(mel_features + i);
        vst1q_f32(dest + i, v);
    }
#else
    std::memcpy(dest, mel_features, INPUT_FEATURES * sizeof(float));
#endif
    
    // Commit con release semantics para visibilidad al worker
    m_head.store(next_head, std::memory_order_release);
    return true;
}

bool AntiDolbyAI::processInference(InferenceResult& out_result) noexcept {
    const size_t current_tail = m_tail.load(std::memory_order_relaxed);
    
    if (current_tail == m_head.load(std::memory_order_acquire)) {
        return false; // Cola vacía
    }
    
    // Extraer datos del Ring Buffer
    const float* input = m_ring_buffer[current_tail].features.data();
    
    // Pipeline neural forward pass
    forwardPass(input, out_result);
    
    // Avanzar tail (consumido)
    m_tail.store((current_tail + 1) % RING_BUFFER_SIZE, std::memory_order_release);
    return true;
}

void AntiDolbyAI::forwardPass(const float* __restrict input, InferenceResult& result) noexcept {
    ALIGN_NEON std::array<float, HIDDEN_DIM> pre_act;
    pre_act.fill(0.0f);

    // GEMV Input-to-Hidden
    for (size_t i = 0; i < INPUT_FEATURES; ++i) {
        const float x = input[i];
        const float* __restrict w = &m_weights_ih[i * HIDDEN_DIM];
#pragma clang loop vectorize(enable) interleave(enable)
        for (size_t h = 0; h < HIDDEN_DIM; ++h) {
            pre_act[h] += x * w[h];
        }
    }

    // LSTM Gates simplificados e iteración de estado (Cell & Hidden)
    for (size_t h = 0; h < HIDDEN_DIM; ++h) {
        const float i_gate = fast_sigmoid(pre_act[h]);
        const float f_gate = fast_sigmoid(pre_act[h] + 1.0f); // Bias de olvido
        const float o_gate = fast_sigmoid(pre_act[h]);
        const float c_tilde = fast_tanh(pre_act[h]);
        
        m_cell_state[h] = f_gate * m_cell_state[h] + i_gate * c_tilde;
        m_hidden_state[h] = o_gate * fast_tanh(m_cell_state[h]);
    }

    // GEMV Hidden-to-Output
    ALIGN_NEON std::array<float, NUM_CLASSES> logits;
    logits.fill(0.0f);
    
    for (size_t h = 0; h < HIDDEN_DIM; ++h) {
        const float h_val = m_hidden_state[h];
        const float* __restrict w = &m_weights_ho[h * NUM_CLASSES];
        for (size_t c = 0; c < NUM_CLASSES; ++c) {
            logits[c] += h_val * w[c];
        }
    }

    // Softmax-like probabilístico y output mapping
    const float sum_exp = std::exp(logits[0]) + std::exp(logits[1]) + std::exp(logits[2]) + std::exp(logits[3]);
    const float inv_sum = 1.0f / (sum_exp + 1e-6f);

    result.voice_score = std::exp(logits[0]) * inv_sum;
    result.music_score = std::exp(logits[1]) * inv_sum;
    result.bass_score  = std::exp(logits[2]) * inv_sum;
    result.silence_score = std::exp(logits[3]) * inv_sum;
}

} // namespace Neuromorphic
} // namespace Ivanna
