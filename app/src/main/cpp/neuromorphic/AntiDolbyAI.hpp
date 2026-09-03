#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <array>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {
namespace Neuromorphic {

/**
 * @brief AntiDolbyAI - Supremacy Core Replacement for YAMNet
 * 
 * Arquitectura TinyML ultra-baja latencia (Kernel-Level) basada en 
 * un Pi-LSTM (Phased-Inertial LSTM) disperso acoplado a un clasificador
 * perceptivo de un solo disparo, evadiendo el overhead masivo de TFLite.
 *
 * Características:
 * - Operación Lock-free (SPSC wait-free ring buffer).
 * - SIMD (NEON) vectorization explícita.
 * - Memoria alineada cache-friendly, cero asignaciones dinámicas post-inicialización.
 */
class AntiDolbyAI {
public:
    static constexpr size_t INPUT_FEATURES = 64; // Mel-bins (simplificado vs YAMNet)
    static constexpr size_t HIDDEN_DIM = 32;     // Espacio latente ultra denso
    static constexpr size_t NUM_CLASSES = 4;     // Voice, Music, Bass, Silence

    struct InferenceResult {
        float voice_score;
        float music_score;
        float bass_score;
        float silence_score;
        uint64_t timestamp_us;
    };

    AntiDolbyAI();
    ~AntiDolbyAI();

    // Eliminar copias y movimientos para anclar los punteros en la caché L1/L2
    AntiDolbyAI(const AntiDolbyAI&) = delete;
    AntiDolbyAI& operator=(const AntiDolbyAI&) = delete;

    /**
     * @brief Inyecta features desde el hilo de audio RT. Completamente wait-free.
     * @param mel_features Puntero alineado a 16 bytes, tamaño INPUT_FEATURES.
     * @return true si se encoló correctamente, false si el buffer está lleno (overrun prevention).
     */
    bool enqueueFeatures(const float* __restrict mel_features) noexcept;

    /**
     * @brief Hilo de inferencia worker. Extrae del ring buffer y procesa.
     * @param out_result Resultado de inferencia procesado.
     * @return true si se procesó un frame, false si no hay data.
     */
    bool processInference(InferenceResult& out_result) noexcept;

private:
    // Pesos dispersos y cuantizados virtualmente
    ALIGN_NEON std::array<float, INPUT_FEATURES * HIDDEN_DIM> m_weights_ih;
    ALIGN_NEON std::array<float, HIDDEN_DIM * HIDDEN_DIM> m_weights_hh;
    ALIGN_NEON std::array<float, HIDDEN_DIM * NUM_CLASSES> m_weights_ho;
    
    // Estado del LSTM
    ALIGN_NEON std::array<float, HIDDEN_DIM> m_cell_state;
    ALIGN_NEON std::array<float, HIDDEN_DIM> m_hidden_state;

    // Lock-free Ring Buffer para IPC entre Hilo RT y Worker
    static constexpr size_t RING_BUFFER_SIZE = 16;
    struct RingNode {
        ALIGN_NEON std::array<float, INPUT_FEATURES> features;
        uint64_t timestamp;
    };
    
    std::array<RingNode, RING_BUFFER_SIZE> m_ring_buffer;
    std::atomic<size_t> m_head{0}; // Escrito por Hilo RT
    std::atomic<size_t> m_tail{0}; // Escrito por Hilo Inferencia

    // Funciones de activación inline SIMD
    inline float fast_sigmoid(float x) const noexcept;
    inline float fast_tanh(float x) const noexcept;
    
    // Kernel de inferencia hardcore
    void forwardPass(const float* __restrict input, InferenceResult& result) noexcept;
};

} // namespace Neuromorphic
} // namespace Ivanna
