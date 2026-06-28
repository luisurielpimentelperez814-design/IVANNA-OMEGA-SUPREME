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

#include "../hexagon/ivanna_fastrpc_client.hpp"
#include <malloc.h>
#include "volterra_h2_symmetric.hpp"
#include "fir_upsampler_engine.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>

// Forward declaration de FIRUpsamplerEngine (implementación en ivanna_npe_engine.cpp)
namespace ivanna { namespace dsp { class FIRUpsamplerEngine; } }

// ============================================================================
// IVANNA NeuroCochlearManifold — Pipeline Final de Procesamiento Espacial
// ============================================================================
// Intercepta el final del pipeline e integra:
//   1. Convolución binaural HRTF (delegada desde FastRPC)
//   2. Upsampling FIR a 768kHz (delegado desde NPE Engine)
//   3. Corrección Volterra H2 simétrica (Frente 4)
// 
// Todo opera en un único hilo de audio con zero-copy entre etapas.
// ============================================================================

namespace ivanna {
namespace dsp {

// Estado del manifold
struct ManifoldState {
    float* buffer_pre_hrtf = nullptr;      // Post-upmixer, pre-HRTF
    float* buffer_post_hrtf = nullptr;     // Post-HRTF, pre-upsampling
    float* buffer_post_up = nullptr;       // Post-upsampling 768kHz
    float* buffer_final = nullptr;         // Post-Volterra, listo para DAC

    size_t block_size = 512;
    size_t upsample_factor = 16;
    size_t channels = 2;

    // Handles a subsistemas
    IvannaFastRpcClient* dsp_client = nullptr;
    FIRUpsamplerEngine* upsampler = nullptr;
    VolterraH2Symmetric* volterra = nullptr;

    std::atomic<bool> pipeline_active{false};
};

static ManifoldState g_manifold;

// ============================================================================
// Inicialización del manifold completo
// ============================================================================

bool neuro_cochlear_manifold_init(
    uint32_t block_size,
    uint32_t sample_rate_in,
    uint32_t sample_rate_out,
    uint32_t channels
) {
    g_manifold.block_size = block_size;
    g_manifold.channels = channels;
    g_manifold.upsample_factor = sample_rate_out / sample_rate_in;

    const size_t align = 64;
    const size_t pre_size = block_size * channels * sizeof(float);
    const size_t post_hrtf_size = block_size * channels * sizeof(float);
    const size_t post_up_size = block_size * g_manifold.upsample_factor * channels * sizeof(float);
    const size_t final_size = post_up_size;

    g_manifold.buffer_pre_hrtf = static_cast<float*>(memalign(align, pre_size));
    g_manifold.buffer_post_hrtf = static_cast<float*>(memalign(align, post_hrtf_size));
    g_manifold.buffer_post_up = static_cast<float*>(memalign(align, post_up_size));
    g_manifold.buffer_final = static_cast<float*>(memalign(align, final_size));

    if (!g_manifold.buffer_pre_hrtf || !g_manifold.buffer_post_hrtf ||
        !g_manifold.buffer_post_up || !g_manifold.buffer_final) {
        return false;
    }

    memset(g_manifold.buffer_pre_hrtf, 0, pre_size);
    memset(g_manifold.buffer_post_hrtf, 0, post_hrtf_size);
    memset(g_manifold.buffer_post_up, 0, post_up_size);
    memset(g_manifold.buffer_final, 0, final_size);

    // Inicializa subsistemas
    g_manifold.dsp_client = new IvannaFastRpcClient();
    HrtfConvolutionConfig hrtf_cfg;
    hrtf_cfg.sample_rate_in = sample_rate_in;
    hrtf_cfg.sample_rate_out = sample_rate_out;
    hrtf_cfg.hrtf_filter_length = 512;
    hrtf_cfg.block_size = block_size;
    hrtf_cfg.num_azimuth_bins = 360;
    hrtf_cfg.num_elevation_bins = 180;
    hrtf_cfg.use_fft_convolution = true;

    if (!g_manifold.dsp_client->initialize(hrtf_cfg)) {
        // Fallback: opera sin DSP, todo en CPU
        delete g_manifold.dsp_client;
        g_manifold.dsp_client = nullptr;
    }

    g_manifold.upsampler = new FIRUpsamplerEngine();
    g_manifold.volterra = new VolterraH2Symmetric(8192, channels);

    g_manifold.pipeline_active.store(true, std::memory_order_release);
    return true;
}

// ============================================================================
// Procesa un bloque completo a través del pipeline
// ============================================================================

void neuro_cochlear_process_block(
    const float* input_left,
    const float* input_right,
    int32_t* output_s32,
    const SpatialPosition& position
) {
    if (!g_manifold.pipeline_active.load(std::memory_order_acquire)) return;

    const size_t N = g_manifold.block_size;
    const size_t up_factor = g_manifold.upsample_factor;
    const size_t up_N = N * up_factor;

    // --- Etapa 1: HRTF Binaural (FastRPC -> Hexagon DSP o fallback CPU) ---
    if (g_manifold.dsp_client && g_manifold.dsp_client->isDSPReady()) {
        g_manifold.dsp_client->delegateBinauralConvolution(
            input_left, input_right,
            g_manifold.buffer_post_hrtf,              // left
            g_manifold.buffer_post_hrtf + up_N,       // right (interleaved planar)
            position, N
        );
    } else {
        // Fallback CPU: copia directa (HRTF aplicado upstream en JS)
        for (size_t i = 0; i < N; ++i) {
            g_manifold.buffer_post_hrtf[i] = input_left[i];
            g_manifold.buffer_post_hrtf[N + i] = input_right[i];
        }
    }

    // --- Etapa 2: Upsampling FIR a 768kHz ---
    // Canal izquierdo
    g_manifold.upsampler->process(
        g_manifold.buffer_post_hrtf,
        g_manifold.buffer_post_up,
        N
    );

    // Canal derecho
    g_manifold.upsampler->process(
        g_manifold.buffer_post_hrtf + N,
        g_manifold.buffer_post_up + up_N,
        N
    );

    // --- Etapa 3: Corrección Volterra H2 Simétrica (Frente 4) ---
    // Intercepta el final del pipeline para control electroacústico absoluto
    g_manifold.volterra->processInterleaved(
        g_manifold.buffer_post_up,
        g_manifold.buffer_final,
        up_N,
        g_manifold.channels
    );

    // --- Etapa 4: Conversión S32_LE bit-perfect para DAC ---
    // Escalado a rango completo int32 sin dithering
    const float scale = 2147483647.0f; // 2^31 - 1
    for (size_t i = 0; i < up_N * g_manifold.channels; ++i) {
        float sample = g_manifold.buffer_final[i];
        // Clamp sin soft-limiting
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        output_s32[i] = static_cast<int32_t>(sample * scale);
    }
}

// ============================================================================
// Teardown
// ============================================================================

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
    g_manifold.upsampler = nullptr;
    g_manifold.volterra = nullptr;
}

} // namespace dsp
} // namespace ivanna
