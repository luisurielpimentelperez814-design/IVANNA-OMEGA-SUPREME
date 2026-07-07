/*
 * ============================================================================
 * IVANNA Singularity V3.1 — VolterraH2Symmetric header
 * ============================================================================
 * Autoría: Luis Uriel Pimentel Pérez (Gore TNS). © 2026. Todos los derechos
 * reservados. No CC0. No dominio público.
 * ============================================================================
 */

#ifndef IVANNA_VOLTERRA_H2_SYMMETRIC_HPP
#define IVANNA_VOLTERRA_H2_SYMMETRIC_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>

namespace ivanna {
namespace dsp {

// VolterraH2Symmetric — corrección no-lineal de orden 2 (kernel simétrico).
// y[n] = sum_k h1[k]*x[n-k] + q_gain * sum_{k<=l} c(k,l)*h2[k,l]*x[n-k]*x[n-l]
// donde c(k,l)=1 si k==l y c(k,l)=2 si k!=l (simetría).
class VolterraH2Symmetric {
public:
    VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels,
                         uint32_t quad_kernel_length = 0);
    ~VolterraH2Symmetric();

    void processInterleaved(
        const float* input,
        float* output,
        uint32_t num_frames,
        uint32_t num_channels
    ) noexcept;

    void updateKernels(
        const float* h1_kernel,
        const float* h2_kernel,
        uint32_t length
    ) noexcept;

    uint32_t quadKernelLength() const noexcept { return m_quad_kernel_length; }

    void setEnabled(bool enabled) noexcept {
        m_enabled.store(enabled, std::memory_order_release);
    }
    bool isEnabled() const noexcept {
        return m_enabled.load(std::memory_order_acquire);
    }

    // v3.1: ganancia del término cuadrático (0..1 típico). Evita que h2
    // domine sobre h1 y pegue el soft-clip. Default 0.25.
    void setQuadGain(float g) noexcept {
        if (g < 0.0f) g = 0.0f;
        if (g > 4.0f) g = 4.0f;
        m_quad_gain.store(g, std::memory_order_release);
    }
    float quadGain() const noexcept {
        return m_quad_gain.load(std::memory_order_acquire);
    }

private:
    uint32_t m_kernel_length;
    uint32_t m_channels;
    uint32_t m_quad_kernel_length;

    float* m_h1 = nullptr;
    float* m_h2 = nullptr;

    float** m_delay_lines = nullptr;
    uint32_t* m_delay_indices = nullptr;

    // v3.1: DC-blocker por canal (HP 1er orden).
    float* m_dc_x1 = nullptr;
    float* m_dc_y1 = nullptr;

    std::atomic<bool>  m_enabled{true};
    std::atomic<bool>  m_kernels_ready{false};
    std::atomic<float> m_quad_gain{0.25f};

    VolterraH2Symmetric(const VolterraH2Symmetric&) = delete;
    VolterraH2Symmetric& operator=(const VolterraH2Symmetric&) = delete;
};

} // namespace dsp
} // namespace ivanna

#endif // IVANNA_VOLTERRA_H2_SYMMETRIC_HPP
