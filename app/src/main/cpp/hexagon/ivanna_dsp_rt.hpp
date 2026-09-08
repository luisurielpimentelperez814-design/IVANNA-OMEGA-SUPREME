#pragma once
// ============================================================================
//  ivanna_dsp_rt.hpp — Contrato público del loader runtime FastRPC (Hexagon)
//  © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
//  ÚNICA fuente de verdad para la API de bajo nivel del DSP Hexagon. La
//  implementación vive en ivanna_dsp.cpp (loader dlopen/dlsym).
//
//  Relación con hexagon_dsp_integration.hpp (API de alto nivel):
//    - ensure_available() / is_available() / active_library() / release()
//      (namespace ivanna::hexagon) son la fachada pública que consume el
//      resto del pipeline (npe_engine, fastrpc_client).
//    - ivanna::hexagon::rt::ensure_loaded() es la operación real que hace la
//      carga perezosa; la fachada pública delega en ella.
//    - ivanna::hexagon::rt::dsp_*() son las operaciones IDL directas sobre el
//      handle del DSP (open/close/process_stereo/set_neuro_params/get_metrics).
//
//  Antes de este header la API rt:: vivía SOLO en el .cpp: nadie podía
//  declararla ni llamarla desde otro TU, y la fachada pública quedó SIN
//  implementación (símbolo indefinido). Este header cierra ese hueco.
// ============================================================================

#include <cstdint>

namespace ivanna { namespace hexagon { namespace rt {

// Carga perezosa thread-safe del loader. Devuelve true si el DSP quedó
// operativo (se cargó libcdsprpc/libadsprpc y se resolvieron los símbolos
// IDL mínimos). Idempotente.
bool ensure_loaded() noexcept;

// Estado ya inicializado (NO fuerza la carga).
bool is_available() noexcept;

// Nombre de la librería nativa cargada ("libcdsprpc.so" / "libadsprpc.so")
// o cadena vacía si ninguna. Solo para logging/telemetría.
const char* active_library() noexcept;

// Libera el handle dlopen. Segura de llamar múltiples veces y sin carga previa.
void release() noexcept;

// ── Operaciones IDL directas sobre el handle del DSP ─────────────────────────
// Todas retornan -1 (de forma segura, sin abortar) si el DSP no está
// disponible; el llamador conmuta entonces a la ruta CPU.

int dsp_open(void** out_handle) noexcept;
int dsp_close(void* handle) noexcept;
int dsp_process_stereo(void* handle,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r,
                       int frames) noexcept;
int dsp_set_neuro_params(void* handle,
                         float alpha, float beta, float gamma, float delta) noexcept;
int dsp_get_metrics(void* handle, float* cpu_load, float* peak_amp) noexcept;

// ── Símbolos IDL extendidos para el cliente FastRPC de alto nivel ────────────
// Firmas C puras (extern "C") resueltas por dlopen/dlsym. Puntero nulo si el
// símbolo no existe en la librería cargada. Estas funciones las consume
// ivanna_fastrpc_client.cpp; antes vivían en un segundo loader paralelo
// (ivanna::dsp::phaseh) que hacía su propio dlopen — ahora hay UNO SOLO.
extern "C" {

using dsp_hrtf_init_fn = int (*)(void* handle,
                                 uint32_t sample_rate_in, uint32_t sample_rate_out,
                                 uint32_t hrtf_filter_len, uint32_t block_size);
using dsp_hrtf_convolve_fn = int (*)(void* handle,
                                     const float* in_l, int in_l_len,
                                     const float* in_r, int in_r_len,
                                     float* out_l, int out_l_len,
                                     float* out_r, int out_r_len,
                                     float azimuth, float elevation,
                                     uint32_t num_frames);
using dsp_fir_init_fn = int (*)(void* handle,
                                uint32_t upsampling_factor, uint32_t filter_len);
using dsp_fir_upsample_fn = int (*)(void* handle,
                                    const float* input, int input_len,
                                    float* output, int output_len,
                                    uint32_t input_frames);

dsp_hrtf_init_fn     dsp_hrtf_init_sym() noexcept;
dsp_hrtf_convolve_fn dsp_hrtf_convolve_sym() noexcept;
dsp_fir_init_fn      dsp_fir_init_sym() noexcept;
dsp_fir_upsample_fn  dsp_fir_upsample_sym() noexcept;

} // extern "C"

}}} // namespace ivanna::hexagon::rt
