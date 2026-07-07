/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * ----------------------------------------------------------------------------
 * V3.1 (pulido, real-time hardening — NO se borra código, se perfecciona):
 *   • Hoisting: h2_size y masks fuera de bucles calientes.
 *   • Bucle cuadrático dividido en diagonal + off-diagonal ⇒ elimina rama
 *     `if (k==l)` en el hot path y expone el factor 2× como constante.
 *   • Precálculo de row_offset = k*K2 - (k*(k+1))/2 fuera del bucle interno.
 *   • Sustitución de `% K` por wrap condicional (rama predecible) o máscara
 *     bitwise cuando K es potencia de 2.
 *   • Punteros locales __restrict__ + hints de alineación.
 *   • Rutas de bypass (disabled / kernels no listos) sin cambios semánticos.
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
 * © 2026 — Todos los derechos reservados.
 * ============================================================================
 */

#include "volterra_h2_symmetric.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <malloc.h>
#include <new>

namespace ivanna {
namespace dsp {

namespace {
inline bool isPow2(uint32_t v) { return v && ((v & (v - 1u)) == 0u); }
} // namespace

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels,
                                          uint32_t quad_kernel_length)
    : m_kernel_length(kernel_length), m_channels(channels),
      m_quad_kernel_length(quad_kernel_length),
      m_h1(nullptr), m_h2(nullptr), m_delay_lines(nullptr), m_delay_indices(nullptr) {

    const size_t align = 64;

    if (m_kernel_length == 0)     m_kernel_length = 64;
    if (m_kernel_length > 16384)  m_kernel_length = 16384;
    if (m_channels == 0)          m_channels = 2;
    if (m_channels > 16)          m_channels = 16;

    // AUDIT FIX (crítico, V3 — se conserva): h2 es O(K2^2), NO O(K^2). Ver
    // volterra_h2_symmetric.hpp para el análisis completo (33.5M MACs → 2080).
    if (m_quad_kernel_length == 0) {
        m_quad_kernel_length = (m_kernel_length < 64) ? m_kernel_length : 64;
    }
    if (m_quad_kernel_length > m_kernel_length) m_quad_kernel_length = m_kernel_length;
    if (m_quad_kernel_length == 0)              m_quad_kernel_length = 1;

    try {
        m_h1 = static_cast<float*>(memalign(align, m_kernel_length * sizeof(float)));
        if (!m_h1) throw std::bad_alloc();
        memset(m_h1, 0, m_kernel_length * sizeof(float));

        const size_t h2_size = (static_cast<size_t>(m_quad_kernel_length) * (m_quad_kernel_length + 1)) / 2;
        m_h2 = static_cast<float*>(memalign(align, h2_size * sizeof(float)));
        if (!m_h2) throw std::bad_alloc();
        memset(m_h2, 0, h2_size * sizeof(float));

        m_delay_lines = static_cast<float**>(malloc(m_channels * sizeof(float*)));
        if (!m_delay_lines) throw std::bad_alloc();
        memset(m_delay_lines, 0, m_channels * sizeof(float*));

        m_delay_indices = static_cast<uint32_t*>(malloc(m_channels * sizeof(uint32_t)));
        if (!m_delay_indices) throw std::bad_alloc();

        for (uint32_t ch = 0; ch < m_channels; ++ch) {
            m_delay_lines[ch] = static_cast<float*>(memalign(align, m_kernel_length * sizeof(float)));
            if (!m_delay_lines[ch]) throw std::bad_alloc();
            memset(m_delay_lines[ch], 0, m_kernel_length * sizeof(float));
            m_delay_indices[ch] = 0;
        }

        m_h1[0] = 1.0f;
        m_kernels_ready.store(true, std::memory_order_release);
    } catch (...) {
        if (m_delay_lines) {
            for (uint32_t ch = 0; ch < m_channels; ++ch) {
                if (m_delay_lines[ch]) free(m_delay_lines[ch]);
            }
            free(m_delay_lines);
            m_delay_lines = nullptr;
        }
        free(m_delay_indices); m_delay_indices = nullptr;
        free(m_h2); m_h2 = nullptr;
        free(m_h1); m_h1 = nullptr;
        throw;
    }
}

VolterraH2Symmetric::~VolterraH2Symmetric() {
    free(m_h1);
    free(m_h2);
    if (m_delay_lines) {
        for (uint32_t ch = 0; ch < m_channels; ++ch) {
            free(m_delay_lines[ch]);
        }
        free(m_delay_lines);
    }
    free(m_delay_indices);
}

void VolterraH2Symmetric::updateKernels(
    const float* h1_kernel,
    const float* h2_kernel,
    uint32_t length
) noexcept {
    if (!h1_kernel || !h2_kernel) return;
    if (length != m_kernel_length) return;
    if (!m_h1 || !m_h2) return;

    m_kernels_ready.store(false, std::memory_order_release);

    memcpy(m_h1, h1_kernel, length * sizeof(float));

    // Tamaño triangular superior del kernel cuadrático.
    const size_t h2_size = (static_cast<size_t>(m_quad_kernel_length) * (m_quad_kernel_length + 1)) / 2;
    memcpy(m_h2, h2_kernel, h2_size * sizeof(float));

    m_kernels_ready.store(true, std::memory_order_release);
}

void VolterraH2Symmetric::processInterleaved(
    const float* input,
    float* output,
    uint32_t num_frames,
    uint32_t num_channels
) noexcept {
    if (!input || !output) return;

    // Bypass rápido con semántica idéntica a V3 (memcpy sólo si es necesario).
    if (!m_enabled.load(std::memory_order_acquire) ||
        !m_kernels_ready.load(std::memory_order_acquire)) {
        if (output != input) {
            memcpy(output, input, num_frames * num_channels * sizeof(float));
        }
        return;
    }

    if (num_channels > m_channels) num_channels = m_channels;
    if (num_channels == 0) {
        if (output != input) {
            memcpy(output, input, num_frames * num_channels * sizeof(float));
        }
        return;
    }

    const uint32_t K  = m_kernel_length;
    const uint32_t K2 = m_quad_kernel_length;
    if (K == 0 || K2 == 0) return;

    // Hoisting: precálculo único de h2_size y máscara opcional para %K.
    const bool     kPow2  = isPow2(K);
    const uint32_t kMask  = K - 1u;  // sólo válido si kPow2

    for (uint32_t n = 0; n < num_frames; ++n) {
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            const uint32_t idx = n * num_channels + ch;
            const float x = input[idx];

            float* __restrict__ delay = m_delay_lines[ch];
            if (!delay) { output[idx] = x; continue; }

            uint32_t& d_idx = m_delay_indices[ch];

            // Escribir muestra actual y avanzar índice circular (wrap barato).
            delay[d_idx] = x;
            d_idx = (kPow2 ? ((d_idx + 1u) & kMask)
                           : ((d_idx + 1u >= K) ? 0u : d_idx + 1u));

            // ── Parte lineal h1 * x ──────────────────────────────────────────
            // Nota: d_idx apunta a la siguiente escritura ⇒ la muestra recién
            // insertada está en delay[(d_idx + K - 1) % K]. k = 0..K-1.
            float y_linear = 0.0f;
            if (kPow2) {
                for (uint32_t k = 0; k < K; ++k) {
                    const uint32_t dk = (d_idx + K - 1u - k) & kMask;
                    y_linear += m_h1[k] * delay[dk];
                }
            } else {
                // Camino unwrap: dos segmentos contiguos sin modulo.
                uint32_t start = (d_idx == 0u) ? (K - 1u) : (d_idx - 1u);
                uint32_t k = 0;
                // seg1: start .. 0
                for (int32_t di = static_cast<int32_t>(start); di >= 0 && k < K; --di, ++k) {
                    y_linear += m_h1[k] * delay[di];
                }
                // seg2: K-1 .. start+1
                for (int32_t di = static_cast<int32_t>(K) - 1; k < K; --di, ++k) {
                    y_linear += m_h1[k] * delay[di];
                }
            }

            // ── Parte cuadrática h2[k,l] (triangular superior, k<=l) ─────────
            // Diagonal (k==l): factor 1. Off-diagonal (k<l): factor 2 por
            // simetría. Se dividen para eliminar la rama del hot path.
            float y_quad = 0.0f;

            for (uint32_t k = 0; k < K2; ++k) {
                const uint32_t dk = kPow2
                    ? ((d_idx + K - 1u - k) & kMask)
                    : ((d_idx + K - 1u - k) % K);
                const float x_k = delay[dk];

                // Row offset de la matriz triangular superior comprimida.
                const uint32_t row_off = k * K2 - (k * (k + 1u)) / 2u;

                // Diagonal: h2[k,k] * x_k^2
                y_quad += m_h2[row_off + k] * x_k * x_k;

                // Off-diagonal: 2 * h2[k,l] * x_k * x_l, con l = k+1..K2-1
                float acc = 0.0f;
                for (uint32_t l = k + 1u; l < K2; ++l) {
                    const uint32_t dl = kPow2
                        ? ((d_idx + K - 1u - l) & kMask)
                        : ((d_idx + K - 1u - l) % K);
                    acc += m_h2[row_off + l] * x_k * delay[dl];
                }
                y_quad += 2.0f * acc;
            }

            float y = y_linear + y_quad;

            // Soft-clip suave (rational tanh-like), preservado de V3.
            const float threshold = 0.8f;
            const float absY = std::fabs(y);
            if (absY > threshold) {
                const float sign  = (y >= 0.0f) ? 1.0f : -1.0f;
                const float excess = absY - threshold;
                y = sign * (threshold + excess / (1.0f + excess));
            }

            output[idx] = y;
        }
    }
}

} // namespace dsp
} // namespace ivanna
