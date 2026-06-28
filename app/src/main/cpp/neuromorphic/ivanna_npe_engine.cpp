/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * FIR Upsampler OPTIMIZADO con Polyphase Filter Bank
 * ============================================================================
 */

#include "dsp/ivanna_fastrpc_client.hpp"
#include <hexagon_nn.h>
#include <hvx_hexagon.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>

namespace ivanna {
namespace dsp {

// Constantes optimizadas
static constexpr uint32_t FIR_TAPS = 2048;           // Reducido de 8192 a 2048 (suficiente calidad)
static constexpr uint32_t UPSAMPLE_FACTOR = 16;
static constexpr uint32_t TAPS_PER_PHASE = FIR_TAPS / UPSAMPLE_FACTOR;  // 128 taps por fase

// Coeficientes polifásicos (reorganizados para acceso secuencial)
static alignas(64) float g_polyphase_coefficients[UPSAMPLE_FACTOR][TAPS_PER_PHASE];
static std::atomic<bool> g_coefficients_initialized{false};

// Ventana Blackman-Harris
static inline float blackmanHarrisWindow(int n, int N) {
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;

    float x = (2.0f * M_PI * n) / (N - 1);
    return a0 - a1 * cosf(x) + a2 * cosf(2.0f * x) - a3 * cosf(3.0f * x);
}

// Genera coeficientes polifásicos
static void generatePolyphaseCoefficients() {
    if (g_coefficients_initialized.load(std::memory_order_acquire)) return;

    const float cutoff = 1.0f / (2.0f * UPSAMPLE_FACTOR);

    // Generar coeficientes FIR completos
    float fir_coeffs[FIR_TAPS];
    for (uint32_t i = 0; i < FIR_TAPS; ++i) {
        int32_t n = static_cast<int32_t>(i) - static_cast<int32_t>(FIR_TAPS / 2);
        float sinc = (n == 0) ? 1.0f : sinf(M_PI * cutoff * n) / (M_PI * cutoff * n);
        float window = blackmanHarrisWindow(i, FIR_TAPS);
        fir_coeffs[i] = sinc * window * cutoff * 2.0f;
    }

    // Reorganizar en estructura polifásica
    for (uint32_t phase = 0; phase < UPSAMPLE_FACTOR; ++phase) {
        for (uint32_t k = 0; k < TAPS_PER_PHASE; ++k) {
            g_polyphase_coefficients[phase][k] = fir_coeffs[phase + k * UPSAMPLE_FACTOR];
        }
    }

    g_coefficients_initialized.store(true, std::memory_order_release);
}

IvannaFastRpcClient::IvannaFastRpcClient() noexcept {
    generatePolyphaseCoefficients();
}

IvannaFastRpcClient::~IvannaFastRpcClient() {
    teardown();
}

bool IvannaFastRpcClient::initialize(const HrtfConvolutionConfig& config) noexcept {
    m_config = config;

    m_dsp_handle = dsp_open();
    if (!m_dsp_handle) {
        m_dsp_handle = adsprpc_open();
        if (!m_dsp_handle) return false;
    }

    m_dma_buffer_size = config.block_size * UPSAMPLE_FACTOR * sizeof(float) * 2;
    m_dma_buffer_in = dsp_alloc_dma(m_dma_buffer_size, m_dsp_handle);
    m_dma_buffer_out = dsp_alloc_dma(m_dma_buffer_size, m_dsp_handle);

    if (!m_dma_buffer_in || !m_dma_buffer_out) {
        teardown();
        return false;
    }

    m_hrtf_convolver = dsp_create_module(m_dsp_handle, "ivanna_hrtf_convolver", &config);
    m_fir_upsampler = dsp_create_module(m_dsp_handle, "ivanna_fir_upsampler", &config);

    m_dsp_ready.store(true, std::memory_order_release);
    m_initialized.store(true, std::memory_order_release);

    return true;
}

void IvannaFastRpcClient::teardown() noexcept {
    m_initialized.store(false, std::memory_order_release);
    m_dsp_ready.store(false, std::memory_order_release);

    if (m_hrtf_convolver) {
        dsp_destroy_module(m_dsp_handle, m_hrtf_convolver);
        m_hrtf_convolver = nullptr;
    }
    if (m_fir_upsampler) {
        dsp_destroy_module(m_dsp_handle, m_fir_upsampler);
        m_fir_upsampler = nullptr;
    }

    if (m_dma_buffer_in) {
        dsp_free_dma(m_dma_buffer_in, m_dsp_handle);
        m_dma_buffer_in = nullptr;
    }
    if (m_dma_buffer_out) {
        dsp_free_dma(m_dma_buffer_out, m_dsp_handle);
        m_dma_buffer_out = nullptr;
    }

    if (m_dsp_handle) {
        dsp_close(m_dsp_handle);
        m_dsp_handle = nullptr;
    }
}

bool IvannaFastRpcClient::delegateBinauralConvolution(
    const float* input_left, const float* input_right,
    float* output_left, float* output_right,
    const SpatialPosition& position,
    uint32_t num_frames
) noexcept {
    if (!m_dsp_ready.load(std::memory_order_acquire)) return false;

    float* dma_in = static_cast<float*>(m_dma_buffer_in);
    for (uint32_t i = 0; i < num_frames; ++i) {
        dma_in[i * 2] = input_left[i];
        dma_in[i * 2 + 1] = input_right[i];
    }

    dsp_invoke(m_dsp_handle, m_hrtf_convolver,
               m_dma_buffer_in, m_dma_buffer_out, num_frames,
               position.azimuth, position.elevation, position.distance);

    float* dma_out = static_cast<float*>(m_dma_buffer_out);
    for (uint32_t i = 0; i < num_frames; ++i) {
        output_left[i] = dma_out[i * 2];
        output_right[i] = dma_out[i * 2 + 1];
    }

    return true;
}

bool IvannaFastRpcClient::delegateFIRUpsampling(
    const float* input, float* output,
    uint32_t input_frames, uint32_t output_frames
) noexcept {
    if (!m_dsp_ready.load(std::memory_order_acquire)) return false;
    if (output_frames != input_frames * UPSAMPLE_FACTOR) return false;

    memcpy(m_dma_buffer_in, input, input_frames * sizeof(float));

    dsp_invoke(m_dsp_handle, m_fir_upsampler,
               m_dma_buffer_in, m_dma_buffer_out, input_frames,
               FIR_TAPS, UPSAMPLE_FACTOR);

    memcpy(output, m_dma_buffer_out, output_frames * sizeof(float));

    return true;
}

float IvannaFastRpcClient::getDSPThermalLoad() const noexcept {
    if (!m_dsp_handle) return 0.0f;
    return dsp_get_thermal_load(m_dsp_handle);
}

// ============================================================================
// FIR Upsampler OPTIMIZADO con Polyphase Filter Bank
// ============================================================================

class FIRUpsamplerEngine {
public:
    FIRUpsamplerEngine() {
        generatePolyphaseCoefficients();
        
        // Delay line con tamaño potencia de 2 para bitmask
        m_delay_size = 1;
        while (m_delay_size < TAPS_PER_PHASE * 2) m_delay_size <<= 1;
        m_delay_mask = m_delay_size - 1;
        
        m_delay_line = static_cast<float*>(aligned_alloc(64, m_delay_size * sizeof(float)));
        memset(m_delay_line, 0, m_delay_size * sizeof(float));
        m_delay_index = 0;
    }

    ~FIRUpsamplerEngine() {
        free(m_delay_line);
    }

    /**
     * Upsampling polifásico optimizado.
     * En lugar de procesar FIR_TAPS taps por muestra de salida,
     * procesa solo TAPS_PER_PHASE (128) taps por muestra.
     * Reducción: 16x menos operaciones.
     */
    __attribute__((optimize("O3"))) void process(const float* input, float* output, uint32_t input_frames) {
        const uint32_t output_frames = input_frames * UPSAMPLE_FACTOR;

        for (uint32_t n = 0; n < input_frames; ++n) {
            // Insertar muestra en delay line con bitmask
            m_delay_line[m_delay_index] = input[n];
            m_delay_index = (m_delay_index + 1) & m_delay_mask;

            // Generar UPSAMPLE_FACTOR muestras usando filtros polifásicos
            #pragma unroll 4
            for (uint32_t phase = 0; phase < UPSAMPLE_FACTOR; ++phase) {
                float accumulator = 0.0f;
                
                // Convolución con filtro polifásico (solo 128 taps en lugar de 8192)
                const float* coeffs = g_polyphase_coefficients[phase];
                
                #pragma unroll 8
                for (uint32_t k = 0; k < TAPS_PER_PHASE; ++k) {
                    uint32_t d = (m_delay_index + m_delay_size - k - 1) & m_delay_mask;
                    accumulator += m_delay_line[d] * coeffs[k];
                }

                output[n * UPSAMPLE_FACTOR + phase] = accumulator;
            }
        }
    }

private:
    float* m_delay_line;
    uint32_t m_delay_index;
    uint32_t m_delay_size;
    uint32_t m_delay_mask;
};

} // namespace dsp
} // namespace ivanna
