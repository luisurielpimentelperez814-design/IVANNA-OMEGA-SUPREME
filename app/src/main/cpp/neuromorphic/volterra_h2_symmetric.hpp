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
#include "../supreme/SupremeTransitionEnvelope.hpp"
#include "volterra_kernel_package.hpp"

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
    VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels,
                       uint32_t sample_rate = 48000u);
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
     * Igual que updateKernels() (misma validación y mismos efectos) pero
     * informa POR QUÉ rechaza: updateKernels() descarta en silencio.
     *
     * Contrato para el consumidor (hilo de CONTROL, nunca el de audio):
     *   1. setEnabled(false)            -> el envelope fundea a silencio
     *   2. esperar isSilent() == true   -> sin callbacks mezclando wet
     *   3. updateKernelsChecked(...) / loadKernelPackage(...) == Ok
     *   4. setEnabled(true)             -> sólo si el estado resultante se desea
     * Con el motor habilitado devuelve EngineBusy sin tocar nada.
     * Nota RT: puede llamar memalign/free (H2 se materializa/libera) por lo que
     * NUNCA debe invocarse desde el callback de audio.
     */
    VolterraLoadStatus updateKernelsChecked(
        const float* h1_kernel,
        const float* h2_kernel,
        uint32_t length
    ) noexcept;

    /**
     * Valida un paquete IVK1 (neuromorphic/volterra_kernel_package.hpp) contra
     * `policy` (expected_kernel_length se fuerza al K de este motor) y, sólo si
     * es Ok, lo aplica con la misma ruta que updateKernelsChecked().
     * Ante CUALQUIER error deja el motor en bypass inmediato (setEnabled(false))
     * y no toca los kernels ya aplicados. En éxito NO habilita el motor.
     * Un paquete sin H2 (flag H2_PRESENT apagado) aplica H2 = 0.
     */
    VolterraLoadStatus loadKernelPackage(
        const uint8_t* package,
        size_t package_size,
        const VolterraLoadPolicy& policy
    ) noexcept;

    uint32_t kernelLength() const noexcept { return m_kernel_length; }

    /**
     * Activa/desactiva la corrección H2 (para bypass A/B testing)
     */
    void setEnabled(bool enabled) noexcept {
        // Solo publica la intención; el envelope pertenece al hilo DSP.
        m_enabled.store(enabled, std::memory_order_release);
    }

    // Publica el sample rate real desde SET_CONFIG; el hilo DSP aplica el cambio
    // al comienzo del siguiente bloque, sin carreras ni reconfiguración en control.
    void requestSampleRate(uint32_t sampleRate) noexcept {
        if (sampleRate >= 8000u && sampleRate <= 768000u) {
            m_pending_sample_rate.store(sampleRate, std::memory_order_release);
        }
    }

    void setThermalBypass(bool thermalBypass) noexcept {
        m_thermal_bypass.store(thermalBypass, std::memory_order_relaxed);
    }

    bool isEnabled() const noexcept {
        return m_enabled.load(std::memory_order_acquire);
    }

    bool hasEffectiveKernel() const noexcept {
        return m_effective_kernel.load(std::memory_order_acquire);
    }

    bool isSilent() const noexcept {
        return m_dsp_silent.load(std::memory_order_acquire);
    }

    float currentTransitionGain() const noexcept {
        return m_published_gain.load(std::memory_order_acquire);
    }

    const ivanna::supreme::SupremeStateContinuityManager& continuityManager() const noexcept {
        return m_continuity_mgr;
    }

    void preserveInterleavedTail(
        const float* input,
        uint32_t num_frames,
        uint32_t num_channels
    ) noexcept;

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

    // Coeficientes mutables solo con ready=false y cero callbacks activos.
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_thermal_bypass{false};
    std::atomic<bool> m_kernels_ready{false};
    std::atomic<bool> m_effective_kernel{false};
    std::atomic<uint32_t> m_active_processes{0};
    std::atomic<uint32_t> m_pending_sample_rate{0u};
    // Publica telemetría DSP sin leer el envelope mutable desde hilos de control.
    std::atomic<bool> m_dsp_silent{true};
    std::atomic<float> m_published_gain{0.0f};
    bool m_h1_identity = true;
    bool m_h2_active = false;
    void releaseStorage() noexcept;
    // h2_kernel == nullptr equivale a H2 = 0 (sólo ruta de paquete).
    VolterraLoadStatus applyKernels(const float* h1_kernel, const float* h2_kernel,
                                    uint32_t length) noexcept;
    ivanna::supreme::SupremeTransitionEnvelope m_transition_env{};
    ivanna::supreme::SupremeStateContinuityManager m_continuity_mgr{};

    // Previene copia
    VolterraH2Symmetric(const VolterraH2Symmetric&) = delete;
    VolterraH2Symmetric& operator=(const VolterraH2Symmetric&) = delete;
};

} // namespace dsp
} // namespace ivanna

#endif // IVANNA_VOLTERRA_H2_SYMMETRIC_HPP
