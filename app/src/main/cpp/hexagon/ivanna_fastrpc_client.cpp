/*
 * ============================================================================
 * IVANNA Singularity V3.0 — FastRPC Client (Hexagon DSP)
 * ============================================================================
 * © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * Implementación del cliente FastRPC para delegación de convolución HRTF
 * binaural y upsampling FIR polifásico al cDSP (Qualcomm Hexagon).
 *
 * Pipeline:
 *   Audio ARM (48kHz) → FastRPC → Hexagon cDSP → HRTF Conv + FIR ↑ → 768kHz
 */

#include "ivanna_fastrpc_client.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ivanna_dsp_open_t)(void** h);
typedef int (*ivanna_dsp_close_t)(void* h);
typedef int (*ivanna_dsp_hrtf_init_t)(
    void* h,
    uint32_t sample_rate_in,
    uint32_t sample_rate_out,
    uint32_t hrtf_filter_len,
    uint32_t block_size
);
typedef int (*ivanna_dsp_hrtf_convolve_t)(
    void* h,
    const float* in_l, int in_l_len,
    const float* in_r, int in_r_len,
    float* out_l, int out_l_len,
    float* out_r, int out_r_len,
    float azimuth, float elevation,
    uint32_t num_frames
);
typedef int (*ivanna_dsp_fir_init_t)(
    void* h,
    uint32_t upsampling_factor,
    uint32_t filter_len
);
typedef int (*ivanna_dsp_fir_upsample_t)(
    void* h,
    const float* input, int input_len,
    float* output, int output_len,
    uint32_t input_frames
);

static ivanna_dsp_open_t g_dsp_open = nullptr;
static ivanna_dsp_close_t g_dsp_close = nullptr;
static ivanna_dsp_hrtf_init_t g_dsp_hrtf_init = nullptr;
static ivanna_dsp_hrtf_convolve_t g_dsp_hrtf_convolve = nullptr;
static ivanna_dsp_fir_init_t g_dsp_fir_init = nullptr;
static ivanna_dsp_fir_upsample_t g_dsp_fir_upsample = nullptr;

#ifdef __cplusplus
}
#endif

namespace ivanna {
namespace dsp {

static std::atomic<bool> g_fastrpc_loaded{false};
static std::atomic<int> g_dsp_refcount{0};

static bool load_fastrpc_symbols() {
    if (g_fastrpc_loaded.load(std::memory_order_acquire)) {
        return (g_dsp_open != nullptr);
    }
    g_fastrpc_loaded.store(true, std::memory_order_release);
    return true;
}

IvannaFastRpcClient::IvannaFastRpcClient() noexcept
    : m_dsp_handle(nullptr),
      m_hrtf_convolver(nullptr),
      m_fir_upsampler(nullptr),
      m_dsp_ready(false),
      m_initialized(false),
      m_dma_buffer_in(nullptr),
      m_dma_buffer_out(nullptr),
      m_dma_buffer_size(0) {
    load_fastrpc_symbols();
}

IvannaFastRpcClient::~IvannaFastRpcClient() {
    teardown();
}

bool IvannaFastRpcClient::initialize(const HrtfConvolutionConfig& config) noexcept {
    if (m_initialized.load(std::memory_order_acquire)) {
        return m_dsp_ready.load(std::memory_order_acquire);
    }

    m_config = config;

    if (g_dsp_open != nullptr) {
        int ret = g_dsp_open(&m_dsp_handle);
        if (ret != 0 || m_dsp_handle == nullptr) {
            m_dsp_ready.store(false, std::memory_order_release);
            m_initialized.store(true, std::memory_order_release);
            return false;
        }
    } else {
        m_dsp_ready.store(false, std::memory_order_release);
        m_initialized.store(true, std::memory_order_release);
        return false;
    }

    if (g_dsp_hrtf_init != nullptr && m_dsp_handle != nullptr) {
        int ret = g_dsp_hrtf_init(
            m_dsp_handle,
            config.sample_rate_in,
            config.sample_rate_out,
            config.hrtf_filter_length,
            config.block_size
        );
        if (ret != 0) {
            goto cleanup;
        }
        m_hrtf_convolver = m_dsp_handle;
    }

    if (g_dsp_fir_init != nullptr && m_dsp_handle != nullptr) {
        uint32_t up_factor = (config.sample_rate_out / config.sample_rate_in);
        if (up_factor == 0) up_factor = 1;
        
        int ret = g_dsp_fir_init(
            m_dsp_handle,
            up_factor,
            512
        );
        if (ret != 0) {
            goto cleanup;
        }
        m_fir_upsampler = m_dsp_handle;
    }

    m_dma_buffer_size = config.block_size * 4 * 2 * sizeof(float);
    m_dma_buffer_in = aligned_alloc(64, m_dma_buffer_size);
    m_dma_buffer_out = aligned_alloc(64, m_dma_buffer_size);

    if (!m_dma_buffer_in || !m_dma_buffer_out) {
        goto cleanup;
    }

    m_dsp_ready.store(true, std::memory_order_release);
    m_initialized.store(true, std::memory_order_release);
    g_dsp_refcount.fetch_add(1, std::memory_order_relaxed);

    return true;

cleanup:
    if (m_dsp_handle != nullptr && g_dsp_close != nullptr) {
        g_dsp_close(m_dsp_handle);
    }
    m_dsp_handle = nullptr;
    m_hrtf_convolver = nullptr;
    m_fir_upsampler = nullptr;
    m_dsp_ready.store(false, std::memory_order_release);
    m_initialized.store(true, std::memory_order_release);
    return false;
}

void IvannaFastRpcClient::teardown() noexcept {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    m_dsp_ready.store(false, std::memory_order_release);

    if (m_dsp_handle != nullptr && g_dsp_close != nullptr) {
        g_dsp_close(m_dsp_handle);
    }

    m_dsp_handle = nullptr;
    m_hrtf_convolver = nullptr;
    m_fir_upsampler = nullptr;

    if (m_dma_buffer_in != nullptr) {
        free(m_dma_buffer_in);
        m_dma_buffer_in = nullptr;
    }
    if (m_dma_buffer_out != nullptr) {
        free(m_dma_buffer_out);
        m_dma_buffer_out = nullptr;
    }

    g_dsp_refcount.fetch_sub(1, std::memory_order_relaxed);
    m_initialized.store(false, std::memory_order_release);
}

bool IvannaFastRpcClient::delegateBinauralConvolution(
    const float* input_left,
    const float* input_right,
    float* output_left,
    float* output_right,
    const SpatialPosition& position,
    uint32_t num_frames
) noexcept {
    if (!m_dsp_ready.load(std::memory_order_acquire)) {
        return false;
    }
    if (!m_hrtf_convolver || !g_dsp_hrtf_convolve) {
        return false;
    }
    if (!input_left || !input_right || !output_left || !output_right) {
        return false;
    }

    if (m_dma_buffer_in && num_frames <= m_config.block_size) {
        memcpy(m_dma_buffer_in, input_left, num_frames * sizeof(float));
        memcpy((char*)m_dma_buffer_in + num_frames * sizeof(float),
               input_right, num_frames * sizeof(float));
    } else {
        m_dma_buffer_in = (void*)input_left;
    }

    int ret = g_dsp_hrtf_convolve(
        m_hrtf_convolver,
        input_left, (int)(num_frames * sizeof(float)),
        input_right, (int)(num_frames * sizeof(float)),
        output_left, (int)(num_frames * sizeof(float)),
        output_right, (int)(num_frames * sizeof(float)),
        position.azimuth,
        position.elevation,
        num_frames
    );

    return (ret == 0);
}

bool IvannaFastRpcClient::delegateFIRUpsampling(
    const float* input,
    float* output,
    uint32_t input_frames,
    uint32_t output_frames
) noexcept {
    if (!m_dsp_ready.load(std::memory_order_acquire)) {
        return false;
    }
    if (!m_fir_upsampler || !g_dsp_fir_upsample) {
        return false;
    }
    if (!input || !output) {
        return false;
    }

    int ret = g_dsp_fir_upsample(
        m_fir_upsampler,
        input, (int)(input_frames * sizeof(float)),
        output, (int)(output_frames * sizeof(float)),
        input_frames
    );

    return (ret == 0);
}

float IvannaFastRpcClient::getDSPThermalLoad() const noexcept {
    return 0.0f;
}

} // namespace dsp
} // namespace ivanna
