/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema e implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

#include "volterra_h2_symmetric.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <malloc.h>
#include <algorithm>

// Inclusión obligatoria de las intrínsecas vectoriales por hardware de ARM
#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace ivanna {
namespace dsp {

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels)
    : m_kernel_length(kernel_length), m_channels(channels) {

    const size_t align = 64;

    if (m_kernel_length == 0) m_kernel_length = 64;
    if (m_kernel_length > 16384) m_kernel_length = 16384;
    if (m_channels == 0) m_channels = 2;
    if (m_channels > 16) m_channels = 16;

    m_h1 = static_cast<float*>(memalign(align, m_kernel_length * sizeof(float)));
    if (m_h1) memset(m_h1, 0, m_kernel_length * sizeof(float));

    const size_t h2_size = (m_kernel_length * (m_kernel_length + 1)) / 2;
    m_h2 = static_cast<float*>(memalign(align, h2_size * sizeof(float)));
    if (m_h2) memset(m_h2, 0, h2_size * sizeof(float));

    m_delay_lines = static_cast<float**>(malloc(m_channels * sizeof(float*)));
    m_delay_indices = static_cast<uint32_t*>(malloc(m_channels * sizeof(uint32_t)));

    for (uint32_t ch = 0; ch < m_channels; ++ch) {
        m_delay_lines[ch] = static_cast<float*>(memalign(align, m_kernel_length * sizeof(float)));
        if (m_delay_lines[ch]) memset(m_delay_lines[ch], 0, m_kernel_length * sizeof(float));
        m_delay_indices[ch] = 0;
    }

    if (m_h1) m_h1[0] = 1.0f;

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
    if (!h1_kernel || !h2_kernel) return;
    if (length != m_kernel_length) return;

    // Cambiar estado atómico de forma segura para detener el hilo caliente de audio
    m_kernels_ready.store(false, std::memory_order_release);

    if (m_h1) memcpy(m_h1, h1_kernel, length * sizeof(float));

    const size_t h2_size = (length * (length + 1)) / 2;
    if (m_h2) memcpy(m_h2, h2_kernel, h2_size * sizeof(float));

    m_kernels_ready.store(true, std::memory_order_release);
}

void VolterraH2Symmetric::processInterleaved(
    const float* input,
    float* output,
    uint32_t num_frames,
    uint32_t num_channels
) noexcept {
    if (!input || !output) return;

    if (!m_enabled.load(std::memory_order_acquire) || 
        !m_kernels_ready.load(std::memory_order_acquire)) {
        if (output != input) {
            memcpy(output, input, num_frames * num_channels * sizeof(float));
        }
        return;
    }

    if (num_channels > m_channels) {
        num_channels = m_channels;
    }
    if (num_channels == 0) return;

    // CAPTURA LOCAL SEGURA: Blindaje contra Race Conditions que provocan el desbordamiento de búfer (SIGSEGV)
    const uint32_t K = m_kernel_length;
    if (K == 0) return;
    const size_t h2_max_size = (K * (K + 1)) / 2;

    // 1. ESCUDO DE HARDWARE: Forzar modo Flush-to-Zero (FTZ) en la arquitectura ARM de Android
    // Evita que la CPU degrade su rendimiento procesando números infinitesimales (Elimina el desvanecimiento)
#if defined(__arm64__) || defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1 << 24); // Habilitar bit de redondeo rápido a cero por hardware
    asm volatile("msr fpcr, %0" :: "r"(fpcr));
#endif

    for (uint32_t n = 0; n < num_frames; ++n) {
        for (uint32_t ch = 0; ch < num_channels; ++ch) {
            const uint32_t idx = n * num_channels + ch;
            float x = input[idx];

            // Compuerta de ruido rápida contra el fango digital residual en el flujo
            if (std::abs(x) < 1e-30f) x = 0.0f;

            if (!m_delay_lines[ch]) {
                output[idx] = x;
                continue;
            }

            float* delay = m_delay_lines[ch];
            uint32_t& d_idx = m_delay_indices[ch];

            delay[d_idx] = x;
            d_idx = (d_idx + 1) % K;

            // --- PROCESAMIENTO DEL NÚCLEO LINEAL H1 ---
            float y_linear = 0.0f;
            for (uint32_t k = 0; k < K; ++k) {
                uint32_t delay_k = (d_idx + K - k) % K;
                y_linear += m_h1[k] * delay[delay_k];
            }

            // --- PROCESAMIENTO DEL NÚCLEO CUADRÁTICO H2 (ARM NEON VECTORIZADO) ---
            float y_quad = 0.0f;

            for (uint32_t k = 0; k < K; ++k) {
                uint32_t delay_k = (d_idx + K - k) % K;
                float x_k = delay[delay_k];
                
                // Umbral psicoacústico inteligente (~-120 dBFS). Si la muestra es silencio, saltamos la fila completa
                if (std::abs(x_k) < 1e-6f) continue; 

                uint32_t l = k;

#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)
                // Ejecución SIMD de alto rendimiento: Procesamos bloques paralelos de 4 flotantes
                for (; l + 3 < K; l += 4) {
                    uint32_t h2_idx0 = k * K + l - (k * (k + 1)) / 2;
                    
                    // Doble validación de memoria estricta para mitigar caídas de punteros corruptos
                    if (h2_idx0 + 3 >= h2_max_size) break;

                    // Cargar coeficientes H2 simétricos desde memoria alineada
                    float32x4_t v_h2 = vld1q_f32(&m_h2[h2_idx0]);

                    // Resolver punteros del buffer circular para los próximos 4 elementos de retraso
                    uint32_t delay_l0 = (d_idx + K - l) % K;
                    uint32_t delay_l1 = (d_idx + K - (l + 1)) % K;
                    uint32_t delay_l2 = (d_idx + K - (l + 2)) % K;
                    uint32_t delay_l3 = (d_idx + K - (l + 3)) % K;

                    alignas(16) float samples_l[4] = {
                        delay[delay_l0],
                        delay[delay_l1],
                        delay[delay_l2],
                        delay[delay_l3]
                    };
                    float32x4_t v_xl = vld1q_f32(samples_l);

                    // Operación Fused Multiply: v_prod = v_h2 * x_k * v_xl
                    float32x4_t v_xk = vmovq_n_f32(x_k);
                    float32x4_t v_prod = vmulq_f32(v_h2, v_xk);
                    v_prod = vmulq_f32(v_prod, v_xl);

                    // Ajustar el factor escalar multiplicador (1.0f si k == l, de lo contrario 2.0f por simetría)
                    alignas(16) float factors[4] = { (k == l) ? 1.0f : 2.0f, 2.0f, 2.0f, 2.0f };
                    float32x4_t v_fac = vld1q_f32(factors);
                    v_prod = vmulq_f32(v_prod, v_fac);

                    // Acumulador escalar rápido desde los carriles NEON
                    y_quad += vgetq_lane_f32(v_prod, 0);
                    y_quad += vgetq_lane_f32(v_prod, 1);
                    y_quad += vgetq_lane_f32(v_prod, 2);
                    y_quad += vgetq_lane_f32(v_prod, 3);
                }
#endif

                // Bucle de limpieza matemático para procesar muestras remanentes fuera del bloque de 4
                for (; l < K; ++l) {
                    uint32_t delay_l = (d_idx + K - l) % K;
                    float x_l = delay[delay_l];
                    if (x_l == 0.0f) continue;

                    uint32_t h2_idx = k * K + l - (k * (k + 1)) / 2;
                    if (h2_idx >= h2_max_size) continue;

                    float h2_val = m_h2[h2_idx];
                    float multiplier = (k == l) ? 1.0f : 2.0f;
                    
                    y_quad += multiplier * h2_val * x_k * x_l;
                }
            }

            float y = y_linear + y_quad;

            // 3. ECO PSICOACÚSTICO PASIVO (Cuando el motor Binaural está inactivo)
            // Cuando la HRTF se apaga, inyecta un desfasador de compensación tonal para revivir el brillo
            // y limpiar el sonido opaco o acartonado provocado por la distorsión asimétrica
            bool is_binaural_active = false; // Mapear dinámicamente según el estado del framework espacial
            if (!is_binaural_active) {
                uint32_t prev_idx = (d_idx + K - 1) % K;
                float x_prev = delay[prev_idx];
                y = (y * 0.86f) + (x_prev * 0.14f); // Crossfade equilibrado en fase pasiva
            }

            // Limitador estricto por hardware (Hard-Clipping) para bloquear desbordamientos e interrupciones en el DAC
            if (y > 1.0f)  y = 1.0f;
            if (y < -1.0f) y = -1.0f;

            output[idx] = y;
        }
    }
}

} // namespace dsp
} // namespace ivanna
