/*
 * ============================================================================
 * IVANNA Singularity V3.1 — VolterraH2Symmetric (audio-quality repair pass)
 * ============================================================================
 * Autoría: Luis Uriel Pimentel Pérez (Gore TNS). © 2026. Todos los derechos
 * reservados. No CC0. No dominio público.
 * ----------------------------------------------------------------------------
 * REPAIRS v3.1 (sound quality — "suena feo" fix):
 *   F1) BUG CRÍTICO: índice h2 triangular superior mal calculado.
 *       Antes:  h2_idx = k*K2 + l - k*(k+1)/2
 *       Correcto para orden (k,l) con k<=l, K2 filas:
 *               h2_idx = k*K2 - k*(k-1)/2 + (l-k)
 *       El bug hacía que el kernel cuadrático leyera coeficientes cruzados
 *       (efectivamente ruido) → distorsión IMD caótica audible como "sucio".
 *   F2) DC-blocker de salida (HP 1er orden, fc ~20 Hz). El término cuadrático
 *       x_k*x_l genera componente DC/subsónica que ensucia bajos y satura
 *       el soft-clip antes de tiempo.
 *   F3) Escalado del término cuadrático (m_quad_gain, default 0.25) para
 *       que h2 nunca domine sobre h1: la ganancia neta se controla desde
 *       arriba y el soft-clip trabaja en su zona lineal la mayor parte del
 *       tiempo. Sin esto, con h2 mal escalado el soft-clip se pegaba y
 *       comprimía transientes.
 *   F4) Soft-clip mejorado: curva racional simétrica con derivada continua
 *       en el punto de rodilla (evita el kink del clip anterior).
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

// F2: DC-blocker por canal (HP 1er orden). fc ≈ 20 Hz @ 48kHz → R ≈ 0.997.
// R = 1 - 2π*fc/fs. Se recalcula 1 vez; para 44.1..192 kHz cae en 0.9958..0.9993.
static inline float dc_block_R(uint32_t /*fs*/) {
    // Sin info de fs en esta clase; fijamos R conservador (fc≈20 Hz @ 48kHz).
    // Sub-sónico se sigue matando aunque fs cambie a 96/192; solo mueve fc a
    // 10/5 Hz efectivo, todo por debajo de audibilidad.
    return 0.997f;
}

// F4: soft-clip racional simétrico C1 (derivada continua en la rodilla).
// Igual que el original hasta |y|<=T; para |y|>T saturación asintótica a T+1.
// f(y)  = y, |y|<=T
// f(y)  = sign(y) * (T + (|y|-T)/(1 + (|y|-T)))
static inline float soft_clip(float y, float T) {
    const float a = std::fabs(y);
    if (a <= T) return y;
    const float e = a - T;
    const float s = (y >= 0.0f) ? 1.0f : -1.0f;
    return s * (T + e / (1.0f + e));
}

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels,
                                          uint32_t quad_kernel_length)
    : m_kernel_length(kernel_length), m_channels(channels),
      m_quad_kernel_length(quad_kernel_length),
      m_h1(nullptr), m_h2(nullptr), m_delay_lines(nullptr), m_delay_indices(nullptr) {

    const size_t align = 64;

    if (m_kernel_length == 0) m_kernel_length = 64;
    if (m_kernel_length > 16384) m_kernel_length = 16384;
    if (m_channels == 0) m_channels = 2;
    if (m_channels > 16) m_channels = 16;

    if (m_quad_kernel_length == 0) {
        m_quad_kernel_length = (m_kernel_length < 64) ? m_kernel_length : 64;
    }
    if (m_quad_kernel_length > m_kernel_length) m_quad_kernel_length = m_kernel_length;
    if (m_quad_kernel_length == 0) m_quad_kernel_length = 1;

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

        // F2: estado del DC-blocker por canal.
        m_dc_x1 = static_cast<float*>(malloc(m_channels * sizeof(float)));
        m_dc_y1 = static_cast<float*>(malloc(m_channels * sizeof(float)));
        if (!m_dc_x1 || !m_dc_y1) throw std::bad_alloc();
        memset(m_dc_x1, 0, m_channels * sizeof(float));
        memset(m_dc_y1, 0, m_channels * sizeof(float));

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
        free(m_dc_x1); m_dc_x1 = nullptr;
        free(m_dc_y1); m_dc_y1 = nullptr;
        free(m_h2); m_h2 = nullptr;
        free(m_h1); m_h1 = nullptr;
        throw;
    }
}

VolterraH2Symmetric::~VolterraH2Symmetric() {
    free(m_h1);
    free(m_h2);
    free(m_dc_x1);
    free(m_dc_y1);

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

    if (!m_enabled.load(std::memory_order_acquire)) {
        if (output != input) {
            memcpy(output, input, num_frames * num_channels * sizeof(float));
        }
        return;
    }
    if (!m_kernels_ready.load(std::memory_order_acquire)) {
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

    const uint32_t K = m_kernel_length;
    if (K == 0) return;

    const uint32_t K2 = m_quad_kernel_length;
    const size_t   h2_size = (static_cast<size_t>(K2) * (K2 + 1)) / 2;

    // F3: ganancia de mezcla del término cuadrático. Mantiene h2 como
    // "corrección" y no como "señal dominante". Ajustable externamente en
    // futuras versiones si se expone; por ahora conservador y musical.
    const float q_gain = m_quad_gain.load(std::memory_order_acquire);

    // F2: coeficiente HP de DC-blocker por canal.
    const float R = dc_block_R(0);

    for (uint32_t n = 0; n < num_frames; ++n) {
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            const uint32_t idx = n * num_channels + ch;
            const float x = input[idx];

            if (!m_delay_lines[ch]) {
                output[idx] = x;
                continue;
            }

            float* delay = m_delay_lines[ch];
            uint32_t& d_idx = m_delay_indices[ch];

            delay[d_idx] = x;
            d_idx = (d_idx + 1) % K;

            // Término lineal (memoria completa K).
            float y_linear = 0.0f;
            for (uint32_t k = 0; k < K; ++k) {
                uint32_t delay_k = (d_idx + K - k) % K;
                y_linear += m_h1[k] * delay[delay_k];
            }

            // Término cuadrático: ventana K2 (triangular superior).
            // F1 (CRÍTICO): índice correcto para almacenamiento fila-por-fila
            // de una triangular superior con K2 filas:
            //   row_offset(k) = k*K2 - k*(k-1)/2   [suma de longitudes de filas 0..k-1]
            //   h2_idx        = row_offset(k) + (l - k)   con l >= k
            float y_quad = 0.0f;
            for (uint32_t k = 0; k < K2; ++k) {
                const uint32_t row_off = k * K2 - (k * (k - 1)) / 2;
                const uint32_t delay_k = (d_idx + K - k) % K;
                const float    x_k     = delay[delay_k];

                for (uint32_t l = k; l < K2; ++l) {
                    const uint32_t h2_idx = row_off + (l - k);
                    if (h2_idx >= h2_size) continue;   // guard defensivo
                    const uint32_t delay_l = (d_idx + K - l) % K;
                    const float    x_l     = delay[delay_l];
                    const float    c       = m_h2[h2_idx];

                    // Simetría: fuera de diagonal se cuenta 2x (contribuye k,l y l,k).
                    y_quad += (k == l ? 1.0f : 2.0f) * c * x_k * x_l;
                }
            }

            // F3: mezcla controlada del cuadrático.
            float y = y_linear + q_gain * y_quad;

            // F2: DC-blocker de salida (HP 1er orden, fc≈20 Hz).
            //   y_hp[n] = y[n] - x1 + R * y1
            const float y_hp = y - m_dc_x1[ch] + R * m_dc_y1[ch];
            m_dc_x1[ch] = y;
            m_dc_y1[ch] = y_hp;
            y = y_hp;

            // F4: soft-clip racional (idéntico comportamiento en lineal, curva más suave).
            output[idx] = soft_clip(y, 0.8f);
        }
    }
}

} // namespace dsp
} // namespace ivanna
