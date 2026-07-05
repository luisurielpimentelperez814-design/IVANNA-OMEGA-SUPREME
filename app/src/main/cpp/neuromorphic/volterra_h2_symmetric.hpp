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

#ifndef IVANNA_VOLTERRA_H2_SYMMETRIC_HPP
#define IVANNA_VOLTERRA_H2_SYMMETRIC_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>

namespace ivanna {
namespace dsp {

// ============================================================================
// VolterraH2Symmetric — Corrección No-Lineal de Orden 2 para Transductores
// ============================================================================
// Calcula en tiempo real la convolución inversa de la resonancia mecánica
// para lograr un control electroacústico absoluto del transductor físico.
// 
// Modelo Volterra de orden 2 con kernels simétricos:
//   y[n] = h1[n] * x[n] + sum_k sum_l h2[k,l] * x[n-k] * x[n-l]
// 
// El kernel h2 simétrico compensa las no-linealidades armónicas del transductor
// (resonancias mecánicas, compresión de aire, distorsión de suspensión).
// ============================================================================

class VolterraH2Symmetric {
public:
    /**
     * Constructor.
     * @param kernel_length Longitud del kernel de memoria (taps)
     * @param channels Número de canales (1 o 2)
     */
    VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels);
    ~VolterraH2Symmetric();

    /**
     * Procesa un buffer interleaved aplicando corrección Volterra H2.
     * 
     * @param input Buffer de entrada interleaved (float)
     * @param output Buffer de salida interleaved (float)
     * @param num_frames Número de frames (muestras por canal)
     * @param num_channels Número de canales
     */
    void processInterleaved(
        const float* input,
        float* output,
        uint32_t num_frames,
        uint32_t num_channels
    ) noexcept;

    /**
     * Actualiza los kernels de corrección basándose en caracterización
     * del transductor (medición de impedancia, respuesta en frecuencia).
     * 
     * @param h1_kernel Kernel lineal (FIR de compensación)
     * @param h2_kernel Kernel cuadrático simétrico (matriz triangular superior)
     * @param length Longitud de los kernels
     */
    void updateKernels(
        const float* h1_kernel,
        const float* h2_kernel,
        uint32_t length
    ) noexcept;

    /**
     * Activa/desactiva la corrección H2 (para bypass A/B testing)
     */
    void setEnabled(bool enabled) noexcept {
        m_enabled.store(enabled, std::memory_order_release);
    }

    bool isEnabled() const noexcept {
        return m_enabled.load(std::memory_order_acquire);
    }

private:
    uint32_t m_kernel_length;
    uint32_t m_channels;

    // Kernel lineal h1 (FIR de compensación)
    float* m_h1 = nullptr;

    // Kernel cuadrático h2 simétrico (almacenado como triangular superior)
    // Tamaño: kernel_length * (kernel_length + 1) / 2
    float* m_h2 = nullptr;

    // Delay lines por canal (circular)
    float** m_delay_lines = nullptr;
    uint32_t* m_delay_indices = nullptr;

    // Estado
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_kernels_ready{false};

    // Previene copia
    VolterraH2Symmetric(const VolterraH2Symmetric&) = delete;
    VolterraH2Symmetric& operator=(const VolterraH2Symmetric&) = delete;
};

} // namespace dsp
} // namespace ivanna

#endif // IVANNA_VOLTERRA_H2_SYMMETRIC_HPP
