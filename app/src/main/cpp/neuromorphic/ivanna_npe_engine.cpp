/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
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

#include "../hexagon/ivanna_fastrpc_client.hpp"
#include <cmath>
#include <cstring>
#include <atomic>
#include <malloc.h>
// #include <hexagon_nn.h>  // Hexagon NN SDK (device only)
// #include <hvx_hexagon_protos.h>  // HVX intrinsics (device only)

// ── Hexagon DSP API stubs ────────────────────────────────────────────────────
// Las librerias reales (libcdsprpc.so / libadsprpc.so) se cargan en runtime via
// dlopen() en dispositivos Qualcomm. Estos stubs permiten compilar sin el SDK
// de Qualcomm; initialize() retornara false y el codigo usara fallback CPU.
static inline void* dsp_open() { return nullptr; }
static inline void* adsprpc_open() { return nullptr; }
static inline void* dsp_alloc_dma(size_t, void*) { return nullptr; }
static inline bool dsp_free_dma(void*, void*) { return false; }
static inline void* dsp_create_module(void*, const char*, const void*) { return nullptr; }
static inline bool dsp_destroy_module(void*, void*) { return false; }
static inline bool dsp_close(void*) { return false; }
static inline float dsp_get_thermal_load(void*) { return 0.0f; }
template<typename... Args>
static inline bool dsp_invoke(void*, void*, Args...) { return false; }
// ─────────────────────────────────────────────────────────────────────────────

namespace ivanna {
namespace dsp {

// ============================================================================
// IVANNA NPE Engine — Upsampling FIR de Fase Lineal a 768kHz
// ============================================================================
// Filtro FIR polinomial con ventana Blackman-Harris.
// Operación concurrente y lock-free sobre Hexagon DSP con HVX.
// ============================================================================

// FIX #36: Reduced FIR_TAPS from 8192 to 1024 for Android real-time performance
// 8192 taps @ 768kHz = ~6.1 GMACs/second, impossible on Android in real-time
// 1024 taps @ 768kHz = ~0.78 GMACs/second, feasible with HVX
static constexpr uint32_t FIR_TAPS = 1024;
static constexpr uint32_t UPSAMPLE_FACTOR = 16; // 48kHz -> 768kHz
static constexpr uint32_t HVX_VECTOR_WIDTH = 128; // bytes (32 floats de 32-bit)

// Coeficientes del filtro (pre-calculados, ventana Blackman-Harris)
// Generados offline con precisión de 64 bits, truncados a 32-bit float
// FIX: alignas on static array - use struct wrapper for NDK compatibility
struct alignas(64) FIRCoefficients { float data[FIR_TAPS]; };
static FIRCoefficients g_fir_coefficients_storage;
static float* g_fir_coefficients = g_fir_coefficients_storage.data;
static std::atomic<bool> g_coefficients_initialized{false};
static std::atomic_flag g_coefficients_lock = ATOMIC_FLAG_INIT;

// Ventana Blackman-Harris de 4 términos
static inline float blackmanHarrisWindow(int n, int N) {
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;

    float x = (2.0f * M_PI * n) / (N - 1);
    return a0 - a1 * cosf(x) + a2 * cosf(2.0f * x) - a3 * cosf(3.0f * x);
}

// Genera coeficientes FIR de fase lineal (sinc windowed)
// FIX #37: Thread-safe initialization with spinlock
static void generateFIRCoefficients() {
    if (g_coefficients_initialized.load(std::memory_order_acquire)) return;

    // Spinlock for thread-safe initialization
    while (g_coefficients_lock.test_and_set(std::memory_order_acquire)) {
        // Spin until lock acquired
        if (g_coefficients_initialized.load(std::memory_order_acquire)) {
            g_coefficients_lock.clear(std::memory_order_release);
            return;
        }
    }

    // Double-check after acquiring lock
    if (g_coefficients_initialized.load(std::memory_order_relaxed)) {
        g_coefficients_lock.clear(std::memory_order_release);
        return;
    }

    const float cutoff = 1.0f / (2.0f * UPSAMPLE_FACTOR); // Frecuencia de corte normalizada

    for (uint32_t i = 0; i < FIR_TAPS; ++i) {
        int32_t n = static_cast<int32_t>(i) - static_cast<int32_t>(FIR_TAPS / 2);
        float sinc = (n == 0) ? 1.0f : sinf(M_PI * cutoff * n) / (M_PI * cutoff * n);
        float window = blackmanHarrisWindow(i, FIR_TAPS);
        g_fir_coefficients[i] = sinc * window * cutoff * 2.0f;
    }

    g_coefficients_initialized.store(true, std::memory_order_release);
    g_coefficients_lock.clear(std::memory_order_release);
}

// ============================================================================
// Implementación de IvannaFastRpcClient
// ============================================================================

IvannaFastRpcClient::IvannaFastRpcClient() noexcept {
    generateFIRCoefficients();
}

IvannaFastRpcClient::~IvannaFastRpcClient() {
    teardown();
}

bool IvannaFastRpcClient::initialize(const HrtfConvolutionConfig& config) noexcept {
    m_config = config;

    // Abre conexión al cDSP (compute DSP) para audio
    // Nota: requiere libcdsprpc.so en tiempo de ejecución
    m_dsp_handle = dsp_open();
    if (!m_dsp_handle) {
        // Fallback a aDSP si cDSP no disponible
        m_dsp_handle = adsprpc_open();
        if (!m_dsp_handle) return false;
    }

    // Aloca buffers DMA compartidos para zero-copy
    m_dma_buffer_size = config.block_size * UPSAMPLE_FACTOR * sizeof(float) * 2; // stereo
    m_dma_buffer_in = dsp_alloc_dma(m_dma_buffer_size, m_dsp_handle);
    m_dma_buffer_out = dsp_alloc_dma(m_dma_buffer_size, m_dsp_handle);

    if (!m_dma_buffer_in || !m_dma_buffer_out) {
        teardown();
        return false;
    }

    // Inicializa módulos DSP
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
    const float* input_left,
    const float* input_right,
    float* output_left,
    float* output_right,
    const SpatialPosition& position,
    uint32_t num_frames
) noexcept {
    if (!m_dsp_ready.load(std::memory_order_acquire)) return false;
    if (!input_left || !input_right || !output_left || !output_right) return false;
    if (num_frames == 0) return false;

    // Copia datos a buffer DMA de entrada
    float* dma_in = static_cast<float*>(m_dma_buffer_in);
    for (uint32_t i = 0; i < num_frames; ++i) {
        dma_in[i * 2] = input_left[i];
        dma_in[i * 2 + 1] = input_right[i];
    }

    // Invoca convolución HRTF en DSP
    // FIX #39: Check dsp_invoke return value
    bool success = dsp_invoke(m_dsp_handle, m_hrtf_convolver,
        m_dma_buffer_in, m_dma_buffer_out, num_frames,
        position.azimuth, position.elevation, position.distance);

    if (!success) return false;

    // Recupera resultados
    float* dma_out = static_cast<float*>(m_dma_buffer_out);
    for (uint32_t i = 0; i < num_frames; ++i) {
        output_left[i] = dma_out[i * 2];
        output_right[i] = dma_out[i * 2 + 1];
    }

    return true;
}

bool IvannaFastRpcClient::delegateFIRUpsampling(
    const float* input,
    float* output,
    uint32_t input_frames,
    uint32_t output_frames
) noexcept {
    if (!m_dsp_ready.load(std::memory_order_acquire)) return false;
    if (!input || !output) return false;
    if (output_frames != input_frames * UPSAMPLE_FACTOR) return false;

    // Copia entrada a DMA
    memcpy(m_dma_buffer_in, input, input_frames * sizeof(float));

    // Invoca upsampling FIR en DSP con HVX aceleración
    bool success = dsp_invoke(m_dsp_handle, m_fir_upsampler,
        m_dma_buffer_in, m_dma_buffer_out, input_frames,
        FIR_TAPS, UPSAMPLE_FACTOR);

    if (!success) return false;

    // Recupera salida upsampled
    memcpy(output, m_dma_buffer_out, output_frames * sizeof(float));

    return true;
}

float IvannaFastRpcClient::getDSPThermalLoad() const noexcept {
    if (!m_dsp_handle) return 0.0f;
    return dsp_get_thermal_load(m_dsp_handle);
}

// ============================================================================
// Implementación FIR Upsampling directo (fallback si DSP no disponible)
// ============================================================================

class FIRUpsamplerEngine {
public:
    FIRUpsamplerEngine() {
        generateFIRCoefficients();
        // Estado del filtro: delay line circular
        m_delay_line = static_cast<float*>(memalign(64, FIR_TAPS * sizeof(float)));
        memset(m_delay_line, 0, FIR_TAPS * sizeof(float));
        m_delay_index = 0;
    }

    ~FIRUpsamplerEngine() {
        free(m_delay_line);
    }

    /**
     * Upsampling FIR de fase lineal con HVX SIMD.
     * Entrada: input_frames a tasa Fs
     * Salida: output_frames = input_frames * UPSAMPLE_FACTOR a tarea Fs * UPSAMPLE_FACTOR
     *
     * Implementación lock-free, sin bloqueos, punteros crudos.
     */
    void process(const float* input, float* output, uint32_t input_frames) {
        if (!input || !output || input_frames == 0) return;

        const uint32_t output_frames = input_frames * UPSAMPLE_FACTOR;

        for (uint32_t n = 0; n < input_frames; ++n) {
            // Inserta muestra en delay line
            m_delay_line[m_delay_index] = input[n];
            m_delay_index = (m_delay_index + 1) % FIR_TAPS;

            // Genera UPSAMPLE_FACTOR muestras de salida (zero-stuffing + FIR)
            for (uint32_t phase = 0; phase < UPSAMPLE_FACTOR; ++phase) {
                float accumulator = 0.0f;

                // Convolución FIR con HVX vectorial
                // Procesa 32 taps por iteración (128 bytes / 4 bytes por float)
                uint32_t tap = phase;
                uint32_t delay_idx = m_delay_index;

                // Versión escalar (HVX requiere compilación con toolchain Qualcomm)
                // En producción: reemplazar con intrinsics HVX
                // FIX #38: Bounds check on tap index
                while (tap < FIR_TAPS) {
                    uint32_t tap_div = tap / UPSAMPLE_FACTOR;
                    if (tap_div >= FIR_TAPS) break;  // Safety check
                    uint32_t d = (delay_idx + FIR_TAPS - tap_div - 1) % FIR_TAPS;
                    accumulator += m_delay_line[d] * g_fir_coefficients[tap];
                    tap += UPSAMPLE_FACTOR;
                }

                output[n * UPSAMPLE_FACTOR + phase] = accumulator;
            }
        }
    }

private:
    float* m_delay_line;
    uint32_t m_delay_index;
};

} // namespace dsp
} // namespace ivanna
