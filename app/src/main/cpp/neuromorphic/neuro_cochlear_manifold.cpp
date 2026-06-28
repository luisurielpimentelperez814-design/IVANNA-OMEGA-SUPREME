/*
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * OPTIMIZACIONES vs original:
 *  1. Conversión S32_LE vectorizada con NEON (eliminada doble rama if/else)
 *  2. Clamp sin branch: saturating usando vmaxq_f32/vminq_f32 + vqmovn
 *  3. Fallback CPU: memcpy planar con tamaño exacto (antes loop manual)
 *  4. pipeline_active con memory_order_acquire/release donde corresponde
 *  5. __restrict__ en punteros de salida para permitir alias analysis
 */

#include "dsp/ivanna_fastrpc_client.hpp"
#include "include/volterra_h2_symmetric.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#include "fir_upsampler_engine.hpp"

namespace ivanna {
namespace dsp {

struct ManifoldState {
    float* buffer_pre_hrtf  = nullptr;
    float* buffer_post_hrtf = nullptr;
    float* buffer_post_up   = nullptr;
    float* buffer_final     = nullptr;

    size_t block_size      = 512;
    size_t upsample_factor = 16;
    size_t channels        = 2;

    IvannaFastRpcClient* dsp_client = nullptr;
    FIRUpsamplerEngine*  upsampler  = nullptr;
    VolterraH2Symmetric* volterra   = nullptr;

    std::atomic<bool> pipeline_active{false};
};

static ManifoldState g_manifold;

bool neuro_cochlear_manifold_init(
    uint32_t block_size,
    uint32_t sample_rate_in,
    uint32_t sample_rate_out,
    uint32_t channels
) {
    g_manifold.block_size      = block_size;
    g_manifold.channels        = channels;
    g_manifold.upsample_factor = sample_rate_out / sample_rate_in;

    const size_t align         = 64;
    const size_t pre_size      = block_size * channels * sizeof(float);
    const size_t post_hrtf_sz  = pre_size;
    const size_t post_up_sz    = block_size * g_manifold.upsample_factor * channels * sizeof(float);

    g_manifold.buffer_pre_hrtf  = static_cast<float*>(aligned_alloc(align, pre_size));
    g_manifold.buffer_post_hrtf = static_cast<float*>(aligned_alloc(align, post_hrtf_sz));
    g_manifold.buffer_post_up   = static_cast<float*>(aligned_alloc(align, post_up_sz));
    g_manifold.buffer_final     = static_cast<float*>(aligned_alloc(align, post_up_sz));

    if (!g_manifold.buffer_pre_hrtf || !g_manifold.buffer_post_hrtf ||
        !g_manifold.buffer_post_up  || !g_manifold.buffer_final)
        return false;

    memset(g_manifold.buffer_pre_hrtf,  0, pre_size);
    memset(g_manifold.buffer_post_hrtf, 0, post_hrtf_sz);
    memset(g_manifold.buffer_post_up,   0, post_up_sz);
    memset(g_manifold.buffer_final,     0, post_up_sz);

    g_manifold.dsp_client = new IvannaFastRpcClient();
    HrtfConvolutionConfig hrtf_cfg;
    hrtf_cfg.sample_rate_in    = sample_rate_in;
    hrtf_cfg.sample_rate_out   = sample_rate_out;
    hrtf_cfg.hrtf_filter_length = 512;
    hrtf_cfg.block_size        = block_size;
    hrtf_cfg.num_azimuth_bins  = 360;
    hrtf_cfg.num_elevation_bins = 180;
    hrtf_cfg.use_fft_convolution = true;

    if (!g_manifold.dsp_client->initialize(hrtf_cfg)) {
        delete g_manifold.dsp_client;
        g_manifold.dsp_client = nullptr;
    }

    g_manifold.upsampler = new FIRUpsamplerEngine();
    g_manifold.volterra  = new VolterraH2Symmetric(8192, channels);

    g_manifold.pipeline_active.store(true, std::memory_order_release);
    return true;
}

void neuro_cochlear_process_block(
    const float* __restrict__ input_left,
    const float* __restrict__ input_right,
    int32_t* __restrict__ output_s32,
    const SpatialPosition& position
) {
    if (!g_manifold.pipeline_active.load(std::memory_order_acquire)) return;

    const size_t N         = g_manifold.block_size;
    const size_t up_factor = g_manifold.upsample_factor;
    const size_t up_N      = N * up_factor;
    const size_t ch        = g_manifold.channels;

    // ── Etapa 1: HRTF Binaural ──────────────────────────────────────────────
    if (g_manifold.dsp_client && g_manifold.dsp_client->isDSPReady()) {
        g_manifold.dsp_client->delegateBinauralConvolution(
            input_left, input_right,
            g_manifold.buffer_post_hrtf,
            g_manifold.buffer_post_hrtf + up_N,
            position, N);
    } else {
        // Fallback: copia planar con memcpy (más rápido que loop manual)
        memcpy(g_manifold.buffer_post_hrtf,     input_left,  N * sizeof(float));
        memcpy(g_manifold.buffer_post_hrtf + N, input_right, N * sizeof(float));
    }

    // ── Etapa 2: Upsampling FIR ─────────────────────────────────────────────
    g_manifold.upsampler->process(g_manifold.buffer_post_hrtf,     g_manifold.buffer_post_up,        N);
    g_manifold.upsampler->process(g_manifold.buffer_post_hrtf + N, g_manifold.buffer_post_up + up_N, N);

    // ── Etapa 3: Corrección Volterra H2 ─────────────────────────────────────
    g_manifold.volterra->processInterleaved(
        g_manifold.buffer_post_up,
        g_manifold.buffer_final,
        up_N, ch);

    // ── Etapa 4: Conversión S32_LE vectorizada ───────────────────────────────
    const size_t total = up_N * ch;
    const float  scale = 2147483647.0f; // 2^31 - 1

#ifdef __aarch64__
    float32x4_t vscale = vdupq_n_f32(scale);
    float32x4_t vone   = vdupq_n_f32( 1.0f);
    float32x4_t vnone  = vdupq_n_f32(-1.0f);

    size_t i = 0;
    size_t blocks = total >> 2;
    for (size_t b = 0; b < blocks; ++b, i += 4) {
        float32x4_t vf  = vld1q_f32(g_manifold.buffer_final + i);
        // Saturating clamp [-1, 1]
        vf = vmaxq_f32(vminq_f32(vf, vone), vnone);
        // Scale to S32
        int32x4_t vi = vcvtq_s32_f32(vmulq_f32(vf, vscale));
        vst1q_s32(output_s32 + i, vi);
    }
    for (; i < total; ++i) {
        float s = g_manifold.buffer_final[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        output_s32[i] = (int32_t)(s * scale);
    }
#else
    for (size_t i = 0; i < total; ++i) {
        float s = g_manifold.buffer_final[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        output_s32[i] = (int32_t)(s * scale);
    }
#endif
}

void neuro_cochlear_manifold_teardown() {
    g_manifold.pipeline_active.store(false, std::memory_order_release);
    free(g_manifold.buffer_pre_hrtf);
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
