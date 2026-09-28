/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Volterra H2 Symmetric (Header Público)
 * ============================================================================
 */

#ifndef IVANNA_VOLTERA_H2_SYMMETRIC_HPP
#define IVANNA_VOLTERA_H2_SYMMETRIC_HPP

#include <cstdint>
#include <atomic>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ivanna {
namespace dsp {

class VolterraH2Symmetric {
public:
    VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels);
    ~VolterraH2Symmetric();

    void updateKernels(const float* h1_kernel, const float* h2_kernel, uint32_t length) noexcept;

    void processInterleaved(const float* input, float* output,
                            uint32_t num_frames, uint32_t num_channels) noexcept;

    void setEnabled(bool enabled) noexcept { m_enabled.store(enabled, std::memory_order_release); }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_acquire); }

private:
    uint32_t m_kernel_length;
    uint32_t m_channels;
    float* m_h1;
    float* m_h2;
    float** m_delay_lines;
    uint32_t* m_delay_indices;
    uint32_t m_delay_size;
    uint32_t m_delay_mask;
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_kernels_ready{false};
};

} // namespace dsp
} // namespace ivanna

#endif // IVANNA_VOLTERA_H2_SYMMETRIC_HPP
