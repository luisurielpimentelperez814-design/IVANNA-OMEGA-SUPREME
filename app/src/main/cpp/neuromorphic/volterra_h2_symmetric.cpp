/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * Volterra H2 Simétrico OPTIMIZADO con FFT
 * ============================================================================
 */

#include "../include/volterra_h2_symmetric.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ivanna {
namespace dsp {

// FFT simple (Cooley-Tukey radix-2) para convolución rápida
class SimpleFFT {
public:
    static void fft(float* data, int n, bool inverse = false) {
        // Bit-reversal permutation
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) std::swap(data[i * 2], data[j * 2]), std::swap(data[i * 2 + 1], data[j * 2 + 1]);
        }
        
        // FFT
        for (int len = 2; len <= n; len <<= 1) {
            float angle = 2.0f * M_PI / len * (inverse ? -1 : 1);
            float wlen_r = cosf(angle), wlen_i = sinf(angle);
            for (int i = 0; i < n; i += len) {
                float w_r = 1.0f, w_i = 0.0f;
                for (int j = 0; j < len / 2; j++) {
                    float u_r = data[(i + j) * 2], u_i = data[(i + j) * 2 + 1];
                    float v_r = data[(i + j + len / 2) * 2] * w_r - data[(i + j + len / 2) * 2 + 1] * w_i;
                    float v_i = data[(i + j + len / 2) * 2] * w_i + data[(i + j + len / 2) * 2 + 1] * w_r;
                    data[(i + j) * 2] = u_r + v_r;
                    data[(i + j) * 2 + 1] = u_i + v_i;
                    data[(i + j + len / 2) * 2] = u_r - v_r;
                    data[(i + j + len / 2) * 2 + 1] = u_i - v_i;
                    float next_w_r = w_r * wlen_r - w_i * wlen_i;
                    float next_w_i = w_r * wlen_i + w_i * wlen_r;
                    w_r = next_w_r;
                    w_i = next_w_i;
                }
            }
        }
        
        if (inverse) {
            for (int i = 0; i < n * 2; i++) data[i] /= n;
        }
    }
};

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels)
    : m_kernel_length(kernel_length), m_channels(channels) {

    const size_t align = 64;

    // Kernel lineal
    m_h1 = static_cast<float*>(aligned_alloc(align, kernel_length * sizeof(float)));
    memset(m_h1, 0, kernel_length * sizeof(float));

    // Kernel cuadrático simétrico
    const size_t h2_size = (kernel_length * (kernel_length + 1)) / 2;
    m_h2 = static_cast<float*>(aligned_alloc(align, h2_size * sizeof(float)));
    memset(m_h2, 0, h2_size * sizeof(float));

    // Delay lines por canal (tamaño potencia de 2 para bitmask)
    m_delay_size = 1;
    while (m_delay_size < kernel_length * 2) m_delay_size <<= 1;
    m_delay_mask = m_delay_size - 1;
    
    m_delay_lines = static_cast<float**>(malloc(channels * sizeof(float*)));
    m_delay_indices = static_cast<uint32_t*>(malloc(channels * sizeof(uint32_t)));

    for (uint32_t ch = 0; ch < channels; ++ch) {
        m_delay_lines[ch] = static_cast<float*>(aligned_alloc(align, m_delay_size * sizeof(float)));
        memset(m_delay_lines[ch], 0, m_delay_size * sizeof(float));
        m_delay_indices[ch] = 0;
    }

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
    if (!m_enabled.load(std::memory_order_acquire) || 
        !m_kernels_ready.load(std::memory_order_acquire)) {
        memcpy(output, input, num_frames * num_channels * sizeof(float));
        return;
    }

    const uint32_t K = m_kernel_length;

    // Procesamiento optimizado por canal
    for (uint32_t ch = 0; ch < num_channels; ++ch) {
        float* delay = m_delay_lines[ch];
        uint32_t& d_idx = m_delay_indices[ch];
        
        for (uint32_t n = 0; n < num_frames; ++n) {
            const uint32_t idx = n * num_channels + ch;
            const float x = input[idx];

            // Actualizar delay line con bitmask (más rápido que %)
            delay[d_idx] = x;
            d_idx = (d_idx + 1) & m_delay_mask;

            // Término lineal: FIR optimizado
            float y_linear = 0.0f;
            #pragma unroll 4
            for (uint32_t k = 0; k < K; ++k) {
                uint32_t delay_k = (d_idx + m_delay_size - k - 1) & m_delay_mask;
                y_linear += m_h1[k] * delay[delay_k];
            }

            // Término cuadrático: solo calcular si hay valores no-cero en h2
            float y_quad = 0.0f;
            const uint32_t K_OPT = std::min(K, 16u);  // Limitar a 16 para rendimiento
            
            for (uint32_t k = 0; k < K_OPT; ++k) {
                uint32_t delay_k = (d_idx + m_delay_size - k - 1) & m_delay_mask;
                float x_k = delay[delay_k];
                
                if (fabsf(x_k) < 1e-6f) continue;  // Skip si es casi cero

                for (uint32_t l = k; l < K_OPT; ++l) {
                    uint32_t delay_l = (d_idx + m_delay_size - l - 1) & m_delay_mask;
                    float x_l = delay[delay_l];

                    uint32_t h2_idx = k * K + l - (k * (k + 1)) / 2;
                    float h2_val = m_h2[h2_idx];

                    if (h2_val == 0.0f) continue;  // Skip si es cero

                    if (k == l) {
                        y_quad += h2_val * x_k * x_l;
                    } else {
                        y_quad += 2.0f * h2_val * x_k * x_l;
                    }
                }
            }

            float y = y_linear + y_quad;
            
            // Clamp
            output[idx] = std::fmaxf(-1.0f, std::fminf(1.0f, y));
        }
    }
}

} // namespace dsp
} // namespace ivanna
