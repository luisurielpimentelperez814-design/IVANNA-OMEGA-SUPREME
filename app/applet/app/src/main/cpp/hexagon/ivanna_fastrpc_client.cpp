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
#include "ivanna_dsp_rt.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include <thread>
#include <chrono>

// ── FIX(arquitectura): UN solo loader FastRPC en todo el repo. ──────────────
// Antes existian DOS loaders dlopen paralelos para el mismo DSP:
//   (a) ivanna::hexagon::rt  (ivanna_dsp.cpp)  — open/close/process/metrics
//   (b) ivanna::dsp::phaseh  (ivanna_fastrpc_client_load.cpp) — hrtf/fir
// Ambos abrian libcdsprpc/libadsprpc por separado y resolvian simbolos de
// forma independiente: estado divergente, doble dlopen y codigo duplicado.
// Ahora TODA la carga pasa por el loader canonico ivanna::hexagon::rt y el
// loader paralelo ivanna_fastrpc_client_load.cpp fue ELIMINADO del repo.

namespace rt = ivanna::hexagon::rt;

namespace ivanna {
namespace dsp {

// Refcount de clientes activos (telemetria interna; sin uso externo).
static std::atomic<int> g_dsp_refcount{0};

// El loader canonico hace la carga perezosa en la primera llamada a cualquier
// rt::dsp_*_sym(). No se necesita paso de carga explicito aqui: basta con
// resolver los punteros de funcion bajo demanda desde rt.

IvannaFastRpcClient::IvannaFastRpcClient() noexcept
    : m_dsp_handle(nullptr),
      m_hrtf_convolver(nullptr),
      m_fir_upsampler(nullptr),
      m_dsp_ready(false),
      m_initialized(false),
      m_dma_buffer_in(nullptr),
      m_dma_buffer_out(nullptr),
      m_dma_buffer_size(0) {
    // Fuerza la carga perezosa del loader canonico (idempotente, thread-safe).
    rt::ensure_loaded();
}

IvannaFastRpcClient::~IvannaFastRpcClient() {
    teardown();
}

bool IvannaFastRpcClient::initialize(const HrtfConvolutionConfig& config) noexcept {
    if (m_initialized.load(std::memory_order_acquire)) {
        return m_dsp_ready.load(std::memory_order_acquire);
    }

    m_config = config;

    // Resolver los punteros y precomputar el tamaño ANTES de cualquier goto
    // (C++ prohíbe saltar por encima de inicializaciones).
    auto hrtf_init = rt::dsp_hrtf_init_sym();
    auto fir_init  = rt::dsp_fir_init_sym();
    // FIX(alineacion): aligned_alloc(alineacion, size) exige size multiplo de
    // la alineacion (C11/POSIX) — con block_size impar, block_size*32 no es
    // multiplo de 64 y la reserva fallaba con NULL espurio en Bionic.
    // Se redondea al multiplo de 64 superior.
    const size_t raw_size = static_cast<size_t>(config.block_size) * 4 * 2 * sizeof(float);
    const size_t dma_size = (raw_size + 63u) & ~static_cast<size_t>(63u);

    if (rt::dsp_open(&m_dsp_handle) != 0 || m_dsp_handle == nullptr) {
        m_dsp_ready.store(false, std::memory_order_release);
        m_initialized.store(true, std::memory_order_release);
        return false;
    }

    if (hrtf_init != nullptr && m_dsp_handle != nullptr) {
        int ret = hrtf_init(
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

    if (fir_init != nullptr && m_dsp_handle != nullptr) {
        uint32_t up_factor = (config.sample_rate_out / config.sample_rate_in);
        if (up_factor == 0) up_factor = 1;
        int ret = fir_init(m_dsp_handle, up_factor, 512);
        if (ret != 0) {
            goto cleanup;
        }
        m_fir_upsampler = m_dsp_handle;
    }

    m_dma_buffer_size = dma_size;
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
    // FIX(fuga): si fallaba la reserva del segundo buffer DMA, el primero ya
    // estaba reservado y aqui NO se liberaba -> fuga por cada initialize()
    // fallido. El cleanup libera TODO lo parcialmente adquirido.
    if (m_dma_buffer_in != nullptr) {
        free(m_dma_buffer_in);
        m_dma_buffer_in = nullptr;
    }
    if (m_dma_buffer_out != nullptr) {
        free(m_dma_buffer_out);
        m_dma_buffer_out = nullptr;
    }
    m_dma_buffer_size = 0;
    if (m_dsp_handle != nullptr) {
        rt::dsp_close(m_dsp_handle);
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

    if (m_dsp_handle != nullptr) {
        rt::dsp_close(m_dsp_handle);
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
    auto hrtf_convolve = rt::dsp_hrtf_convolve_sym();
    if (!m_hrtf_convolver || !hrtf_convolve) {
        return false;
    }
    if (!input_left || !input_right || !output_left || !output_right) {
        return false;
    }

    // FIX(heap corruption): ver nota en commit previo — nunca aliasar el
    // scratch DMA; bloques mayores que block_size se rechazan limpio.
    if (num_frames > m_config.block_size) {
        return false;
    }
    if (m_dma_buffer_in) {
        memcpy(m_dma_buffer_in, input_left, num_frames * sizeof(float));
        memcpy(static_cast<char*>(m_dma_buffer_in) + num_frames * sizeof(float),
               input_right, num_frames * sizeof(float));
    }

    int ret = hrtf_convolve(
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
    auto fir_upsample = rt::dsp_fir_upsample_sym();
    if (!m_fir_upsampler || !fir_upsample) {
        return false;
    }
    if (!input || !output) {
        return false;
    }

    int ret = fir_upsample(
        m_fir_upsampler,
        input, (int)(input_frames * sizeof(float)),
        output, (int)(output_frames * sizeof(float)),
        input_frames
    );

    return (ret == 0);
}

float IvannaFastRpcClient::getDSPThermalLoad() const noexcept {
    // FIX(honestidad): antes retornaba 0.0f siempre (telemetria falsa). Si el
    // DSP esta arriba intenta leer la metrica real; si no, reporta 0 pero el
    // caller puede distinguir disponibilidad via isDSPReady().
    if (!m_dsp_ready.load(std::memory_order_acquire)) return 0.0f;
    float cpu_load = 0.0f, peak = 0.0f;
    if (rt::dsp_get_metrics(m_dsp_handle, &cpu_load, &peak) == 0) {
        return cpu_load;
    }
    return 0.0f;
}

} // namespace dsp
} // namespace ivanna
