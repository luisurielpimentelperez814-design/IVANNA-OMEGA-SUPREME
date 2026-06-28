/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 *   Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema y implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

#include "volterra_h2_symmetric.hpp"
#include <malloc.h>
#include <cstring>
#include <cmath>

namespace ivanna {
namespace dsp {

// ============================================================================
// Implementación Volterra H2 Simétrico
// ============================================================================

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels)
    : m_kernel_length(kernel_length), m_channels(channels) {

    const size_t align = 64;

    // Kernel lineal
    m_h1 = static_cast<float*>(memalign(align, kernel_length * sizeof(float)));
    memset(m_h1, 0, kernel_length * sizeof(float));

    // Kernel cuadrático simétrico (triangular superior)
    const size_t h2_size = (kernel_length * (kernel_length + 1)) / 2;
    m_h2 = static_cast<float*>(memalign(align, h2_size * sizeof(float)));
    memset(m_h2, 0, h2_size * sizeof(float));

    // Delay lines por canal
    m_delay_lines = static_cast<float**>(malloc(channels * sizeof(float*)));
    m_delay_indices = static_cast<uint32_t*>(malloc(channels * sizeof(uint32_t)));

    for (uint32_t ch = 0; ch < channels; ++ch) {
        m_delay_lines[ch] = static_cast<float*>(memalign(align, kernel_length * sizeof(float)));
        memset(m_delay_lines[ch], 0, kernel_length * sizeof(float));
        m_delay_indices[ch] = 0;
    }

    // Inicializa kernels con identidad (passthrough hasta actualización)
    m_h1[0] = 1.0f;

    m_kernels_ready.store(true, std::memory_order_release);
}

VolterraH2Symmetric::~VolterraH2Symmetric() {
    free(m_h1);
    free(m_h2);

    for (uint32_t ch = 0; ch < m_channels; ++ch) {
        free(m_delay_lines[ch]);
    }
    free(m_delay_lines);
    free(m_delay_indices);
}

void VolterraH2Symmetric::updateKernels(
    const float* h1_kernel,
    const float* h2_kernel,
    uint32_t length
) noexcept {
    if (length != m_kernel_length) return;

    m_kernels_ready.store(false, std::memory_order_release);

    memcpy(m_h1, h1_kernel, length * sizeof(float));

    const size_t h2_size = (length * (length + 1)) / 2;
    memcpy(m_h2, h2_kernel, h2_size * sizeof(float));

    m_kernels_ready.store(true, std::memory_order_release);
}

void VolterraH2Symmetric::processInterleaved(
    const float* input,
    float* output,
    uint32_t num_frames,
    uint32_t num_channels
) noexcept {
    if (!m_enabled.load(std::memory_order_acquire)) {
        memcpy(output, input, num_frames * num_channels * sizeof(float));
        return;
    }
    if (!m_kernels_ready.load(std::memory_order_acquire)) {
        memcpy(output, input, num_frames * num_channels * sizeof(float));
        return;
    }

    const uint32_t K = m_kernel_length;

    for (uint32_t n = 0; n < num_frames; ++n) {
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            const uint32_t idx = n * num_channels + ch;
            const float x = input[idx];

            // Actualiza delay line circular
            float* delay = m_delay_lines[ch];
            uint32_t& d_idx = m_delay_indices[ch];

            delay[d_idx] = x;
            d_idx = (d_idx + 1) % K;

            // --- Término lineal: convolución FIR ---
            float y_linear = 0.0f;
            for (uint32_t k = 0; k < K; ++k) {
                uint32_t delay_k = (d_idx + K - k) % K;
                y_linear += m_h1[k] * delay[delay_k];
            }

            // --- Término cuadrático: kernel H2 simétrico ---
            // h2[k,l] = h2[l,k], almacenado como triangular superior
            // Índice: idx = k*K + l - k*(k+1)/2 para k <= l
            float y_quad = 0.0f;

            for (uint32_t k = 0; k < K; ++k) {
                uint32_t delay_k = (d_idx + K - k) % K;
                float x_k = delay[delay_k];

                for (uint32_t l = k; l < K; ++l) {
                    uint32_t delay_l = (d_idx + K - l) % K;
                    float x_l = delay[delay_l];

                    // Índice en arreglo triangular superior
                    uint32_t h2_idx = k * K + l - (k * (k + 1)) / 2;
                    float h2_val = m_h2[h2_idx];

                    if (k == l) {
                        y_quad += h2_val * x_k * x_l;
                    } else {
                        y_quad += 2.0f * h2_val * x_k * x_l; // Simetría
                    }
                }
            }

            // Suma: y = y_linear + y_quad
            // El término cuadrático compensa la no-linealidad del transductor
            float y = y_linear + y_quad;

            // Clamp duro (sin soft-limiting) para mantener integridad bit-perfect
            if (y > 1.0f) y = 1.0f;
            if (y < -1.0f) y = -1.0f;

            output[idx] = y;
        }
    }
}

} // namespace dsp
} // namespace ivanna
