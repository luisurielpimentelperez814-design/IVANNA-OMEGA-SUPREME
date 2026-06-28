/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * NeuroCochlearManifold OPTIMIZADO
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

#include "dsp/ivanna_fastrpc_client.hpp"
#include "include/volterra_h2_symmetric.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>

namespace ivanna { namespace dsp { class FIRUpsamplerEngine; } }

namespace ivanna {
namespace dsp {

struct alignas(64) ManifoldState {
    float* buffer_post_hrtf = nullptr;
    float* buffer_post_up   = nullptr;
    float* buffer_final     = nullptr;

    size_t block_size        = 512;
    size_t upsample_factor   = 16;
    size_t channels          = 2;
    size_t up_N              = 8192;

    IvannaFastRpcClient* dsp_client = nullptr;
    FIRUpsamplerEngine*  upsampler   = nullptr;
    VolterraH2Symmetric*  volterra    = nullptr;

    std::atomic<bool> pipeline_active{false};
};

static ManifoldState g_manifold;

bool neuro_cochlear_manifold_init(
    uint32_t block_size,
    uint32_t sample_rate_in,
    uint32_t sample_rate_out,
    uint32_t channels
) {
    if (block_size == 0 || sample_rate_in == 0 || sample_rate_out == 0 || channels == 0)
        return false;

    g_manifold.block_size      = block_size;
    g_manifold.channels        = channels;
    g_manifold.upsample_factor = sample_rate_out / sample_rate_in;
    g_manifold.up_N            = block_size * g_manifold.upsample_factor;

    const size_t align           = 64;
    const size_t post_hrtf_size  = block_size * channels * sizeof(float);
    const size_t post_up_size    = g_manifold.up_N * channels * sizeof(float);
    const size_t final_size      = post_up_size;

    g_manifold.buffer_post_hrtf = static_cast<float*>(aligned_alloc(align, post_hrtf_size));
    g_manifold.buffer_post_up   = static_cast<float*>(aligned_alloc(align, post_up_size));
    g_manifold.buffer_final     = static_cast<float*>(aligned_alloc(align, final_size));

    if (!g_manifold.buffer_post_hrtf || !g_manifold.buffer_post_up || !g_manifold.buffer_final)
        return false;

    g_manifold.dsp_client = new IvannaFastRpcClient();
    HrtfConvolutionConfig hrtf_cfg{};
    hrtf_cfg.sample_rate_in       = sample_rate_in;
    hrtf_cfg.sample_rate_out      = sample_rate_out;
    hrtf_cfg.hrtf_filter_length   = 512;
    hrtf_cfg.block_size           = block_size;
    hrtf_cfg.num_azimuth_bins     = 360;
    hrtf_cfg.num_elevation_bins   = 180;
    hrtf_cfg.use_fft_convolution  = true;

    if (!g_manifold.dsp_client->initialize(hrtf_cfg)) {
        delete g_manifold.dsp_client;
        g_manifold.dsp_client = nullptr;
    }

    g_manifold.upsampler = new FIRUpsamplerEngine();
    g_manifold.volterra  = new VolterraH2Symmetric(g_manifold.up_N, channels);

    g_manifold.pipeline_active.store(true, std::memory_order_release);
    return true;
}

__attribute__((hot, flatten))
void neuro_cochlear_process_block(
    const float* __restrict__ input_left,
    const float* __restrict__ input_right,
    int32_t* __restrict__       output_s32,
    const SpatialPosition&      position
) {
    if (!input_left || !input_right || !output_s32) return;
    if (!g_manifold.pipeline_active.load(std::memory_order_acquire)) return;

    const size_t N        = g_manifold.block_size;
    const size_t up_N     = g_manifold.up_N;
    const size_t channels = g_manifold.channels;
    const size_t total_samples = up_N * channels;

    if (g_manifold.dsp_client && g_manifold.dsp_client->isDSPReady()) {
        g_manifold.dsp_client->delegateBinauralConvolution(
            input_left, input_right,
            g_manifold.buffer_post_hrtf,
            g_manifold.buffer_post_hrtf + N,
            position, N
        );
    } else {
        memcpy(g_manifold.buffer_post_hrtf, input_left, N * sizeof(float));
        memcpy(g_manifold.buffer_post_hrtf + N, input_right, N * sizeof(float));
    }

    g_manifold.upsampler->process(
        g_manifold.buffer_post_hrtf,
        g_manifold.buffer_post_up,
        N
    );
    g_manifold.upsampler->process(
        g_manifold.buffer_post_hrtf + N,
        g_manifold.buffer_post_up + up_N,
        N
    );

    g_manifold.volterra->processInterleaved(
        g_manifold.buffer_post_up,
        g_manifold.buffer_final,
        up_N,
        channels
    );

    const float scale = 2147483647.0f;
    const float* __restrict__ src = g_manifold.buffer_final;
    int32_t* __restrict__ dst     = output_s32;

    #pragma clang loop vectorize(enable) interleave(enable)
    for (size_t i = 0; i < total_samples; ++i) {
        float sample = src[i];
        sample = fminf(1.0f, fmaxf(-1.0f, sample));
        dst[i] = static_cast<int32_t>(sample * scale);
    }
}

void neuro_cochlear_manifold_teardown() {
    g_manifold.pipeline_active.store(false, std::memory_order_release);

    free(g_manifold.buffer_post_hrtf);
    free(g_manifold.buffer_post_up);
    free(g_manifold.buffer_final);

    delete g_manifold.dsp_client;
    delete g_manifold.upsampler;
    delete g_manifold.volterra;

    g_manifold.dsp_client = nullptr;
    g_manifold.upsampler  = nullptr;
    g_manifold.volterra   = nullptr;
}

} // namespace dsp
} // namespace ivanna
