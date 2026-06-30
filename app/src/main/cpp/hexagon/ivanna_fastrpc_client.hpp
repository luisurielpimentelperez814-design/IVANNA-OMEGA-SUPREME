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

#ifndef IVANNA_FASTRPC_CLIENT_HPP
#define IVANNA_FASTRPC_CLIENT_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>

// FastRPC stubs para delegación al Hexagon DSP de Qualcomm
// Requiere librerías DSP de Qualcomm (libadsprpc.so / libcdsprpc.so)

namespace ivanna {
namespace dsp {

// Configuración de convolución binaural HRTF paramétrico
struct HrtfConvolutionConfig {
    uint32_t sample_rate_in;       // Tasa de entrada (ej: 48000)
    uint32_t sample_rate_out;      // Tasa de salida objetivo (768000)
    uint32_t hrtf_filter_length;   // Longitud del filtro HRTF en taps
    uint32_t block_size;           // Tamaño de bloque para procesamiento
    uint32_t num_azimuth_bins;     // Bins de azimuth (ej: 360)
    uint32_t num_elevation_bins;   // Bins de elevación (ej: 180)
    bool use_fft_convolution;      // true para FFT, false para directa
};

// Descriptor de posición espacial para HRTF
struct SpatialPosition {
    float azimuth;     // -180 a 180 grados
    float elevation;   // -90 a 90 grados
    float distance;    // metros
};

// Interfaz de cliente FastRPC para Hexagon DSP
class IvannaFastRpcClient {
public:
    IvannaFastRpcClient() noexcept;
    ~IvannaFastRpcClient();

    // Inicializa conexión con el DSP (cDSP para audio de baja latencia)
    bool initialize(const HrtfConvolutionConfig& config) noexcept;

    // Libera recursos del DSP
    void teardown() noexcept;

    // Delega convolución binaural cruzada al DSP
    // input_left/right: buffers de entrada interleaved o planar
    // output_left/right: buffers de salida
    // position: posición espacial del objeto sonoro
    // Retorna true si el DSP procesó el bloque exitosamente
    bool delegateBinauralConvolution(
        const float* input_left,
        const float* input_right,
        float* output_left,
        float* output_right,
        const SpatialPosition& position,
        uint32_t num_frames
    ) noexcept;

    // Delega upsampling FIR de fase lineal al DSP
    // input: buffer de entrada a tasa inferior
    // output: buffer de salida a 768kHz (factor de upsampling implícito)
    bool delegateFIRUpsampling(
        const float* input,
        float* output,
        uint32_t input_frames,
        uint32_t output_frames
    ) noexcept;

    // Verifica estado del DSP
    bool isDSPReady() const noexcept { return m_dsp_ready.load(std::memory_order_acquire); }

    // Obtiene carga térmica actual del DSP (para mutación algorítmica)
    float getDSPThermalLoad() const noexcept;

private:
    void* m_dsp_handle = nullptr;          // Handle opaco al DSP
    void* m_hrtf_convolver = nullptr;      // Handle al módulo HRTF
    void* m_fir_upsampler = nullptr;       // Handle al módulo FIR

    std::atomic<bool> m_dsp_ready{false};
    std::atomic<bool> m_initialized{false};

    HrtfConvolutionConfig m_config{};

    // Buffers DMA compartidos (ION/DMA-BUF)
    void* m_dma_buffer_in = nullptr;
    void* m_dma_buffer_out = nullptr;
    size_t m_dma_buffer_size = 0;

    // Previene copia
    IvannaFastRpcClient(const IvannaFastRpcClient&) = delete;
    IvannaFastRpcClient& operator=(const IvannaFastRpcClient&) = delete;
};

} // namespace dsp
} // namespace ivanna

#endif // IVANNA_FASTRPC_CLIENT_HPP
