// ════════════════════════════════════════════════════════════════════════
// AVISO (auditoria 2026-08-08): este archivo NO se compila.
// app/src/main/cpp/CMakeLists.txt linea ~162 tiene "EvolutionaryEQ.cpp"
// comentado dentro del bloque IvannaLab. El kernel evolutivo que SI corre
// en produccion es app/src/main/cpp/evolutionary_kernel.cpp — una API en
// C independiente (extern "C", evo_initialize_population/evo_evolve_generation/
// GENOME_SIZE=256), sin relacion con la clase C++ Ivanna::EvolutionaryEQ
// de este archivo.
// Existen 3 copias de EvolutionaryEQ.{cpp,hpp} en el repo (raiz,
// native_kernel/, app/src/main/cpp/) y ya DIVERGIERON entre si (diff -q
// confirma diferencias en las 3 comparaciones cruzadas). Ninguna esta
// enlazada al target ivanna_omega.
// Regla de oro del proyecto — no se borra, solo se anota — asi que este
// archivo se conserva como referencia historica. Si vas a tocar el kernel
// evolutivo real, edita evolutionary_kernel.cpp/.h.
// ════════════════════════════════════════════════════════════════════════

#include "EvolutionaryEQ.hpp"
#include <cmath>
#include <random>

namespace Ivanna {

EvolutionaryEQ::EvolutionaryEQ() : m_stepSize(0.1f) {
    // Initialize FIR coefficients as Dirac impulse delta (unit pass-through)
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_firCoeffsL[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
        m_firCoeffsR[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
    }

    // Zero-fill history ring buffers
    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    // Zero-fill evolutionary genome
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] = 0.0f;
        m_evolutionPath[i] = 0.0f;
    }
}

void EvolutionaryEQ::calibrateTargetRoom() {
    // Impulse generation and genome LM-CMA-ES optimization to flatten target response
    updateLM_CMA_ES();
}

float EvolutionaryEQ::calculateFitness(const float* genome) {
    // Perceptual fitness function penalizing phase divergence and harsh frequency variance
    float fitness = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        float diff = genome[i] - genome[i - 1];
        fitness += diff * diff; // Smooth spectral transition constraint
    }
    return -fitness; // Maximize towards zero
}

void EvolutionaryEQ::updateLM_CMA_ES() {
    constexpr size_t lambda = 4; // Compact population for fast ARM convergence
    float population[lambda][BANDS_512];
    float fitness[lambda];

    // Fast xorshift pseudo-random generator
    uint32_t seed = 12345;
    auto fast_rand = [&]() -> float {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (static_cast<float>(seed % 1000) / 500.0f) - 1.0f;
    };

    // Mutation
    for (size_t p = 0; p < lambda; ++p) {
        for (size_t i = 0; i < BANDS_512; ++i) {
            population[p][i] = m_meanGenome[i] + m_stepSize * fast_rand();
        }
        fitness[p] = calculateFitness(population[p]);
    }

    // Selection (Elitism)
    size_t best_idx = 0;
    for (size_t p = 1; p < lambda; ++p) {
        if (fitness[p] > fitness[best_idx]) {
            best_idx = p;
        }
    }

    // Update Mean and Evolution Path
    const float cc = 0.1f;
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] += 0.5f * (population[best_idx][i] - m_meanGenome[i]);
        m_evolutionPath[i] = (1.0f - cc) * m_evolutionPath[i] + cc * m_meanGenome[i];
    }
}

void EvolutionaryEQ::processNEON(AudioBuffer* buffer) {
    // Time-domain FIR 256-Tap convolution with NEON vectorization
    // Copy new audio block to the end of the history ring buffer
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float32x4_t sumL = vdupq_n_f32(0.0f);
        float32x4_t sumR = vdupq_n_f32(0.0f);

        // Process 4 FIR taps per iteration
        for (size_t t = 0; t < FIR_TAPS; t += 4) {
            float32x4_t hL = vld1q_f32(&m_firCoeffsL[t]);
            float32x4_t hR = vld1q_f32(&m_firCoeffsR[t]);

            float32x4_t xL = vld1q_f32(&m_histL[i + t]);
            float32x4_t xR = vld1q_f32(&m_histR[i + t]);

            sumL = vmlaq_f32(sumL, hL, xL);
            sumR = vmlaq_f32(sumR, hR, xR);
        }

        // Horizontal vector sum reduction
        float l_out = vgetq_lane_f32(sumL, 0) + vgetq_lane_f32(sumL, 1) + vgetq_lane_f32(sumL, 2) + vgetq_lane_f32(sumL, 3);
        float r_out = vgetq_lane_f32(sumR, 0) + vgetq_lane_f32(sumR, 1) + vgetq_lane_f32(sumR, 2) + vgetq_lane_f32(sumR, 3);

        buffer->left[i] = l_out;
        buffer->right[i] = r_out;
    }
#else
    // Scalar convolution fallback
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float l_out = 0.0f;
        float r_out = 0.0f;
        for (size_t t = 0; t < FIR_TAPS; ++t) {
            l_out += m_firCoeffsL[t] * m_histL[i + t];
            r_out += m_firCoeffsR[t] * m_histR[i + t];
        }
        buffer->left[i] = l_out;
        buffer->right[i] = r_out;
    }
#endif

    // Shift history state for next block
    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
