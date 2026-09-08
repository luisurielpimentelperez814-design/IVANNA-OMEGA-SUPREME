// ============================================================================
//  ivanna_dsp.h — Stub generado por qaic desde ivanna_dsp.idl
//  © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
//  IMPORTANTE: Este es un stub de COMPILACIÓN sin el Hexagon SDK. Sus
//  wrappers inline retornan -1 siempre: NUNCA representan el estado real
//  del DSP en runtime. Para saber si el Hexagon está disponible de verdad
//  usa la fachada ivanna::hexagon::ensure_available() (hexagon_dsp_integration.hpp)
//  o la API de bajo nivel ivanna::hexagon::rt (ivanna_dsp_rt.hpp), que hacen
//  dlopen/dlsym real sobre libcdsprpc/libadsprpc. NO uses estos wrappers para
//  lógica de disponibilidad.
//
//  Para habilitar el offloading real al cDSP:
//    1. Instala el Hexagon SDK 5.5 en /opt/Hexagon_SDK/5.5
//    2. Ejecuta: qaic -mdll -I${HEXAGON_SDK}/incs dsp/ivanna_dsp.idl
//    3. Reemplaza este archivo con el generado por qaic
//    4. Pasa -DHEXAGON_SDK_PATH=/opt/Hexagon_SDK/5.5 a CMake
// ============================================================================
#pragma once

#include "ivanna_dsp_rt.hpp"  // redirige al contrato runtime real

#ifdef __cplusplus
extern "C" {
#endif

// Stub de la interfaz generada por qaic. SOLO compila; NO es la ruta runtime.
typedef struct _ivanna_dsp_handle_t* ivanna_dsp_handle_t;

static inline int ivanna_dsp_open(ivanna_dsp_handle_t* h)           { (void)h; return -1; }
static inline int ivanna_dsp_close(ivanna_dsp_handle_t h)           { (void)h; return -1; }
static inline int ivanna_dsp_process_stereo(
    ivanna_dsp_handle_t h,
    const float* in_l, const float* in_r,
    float* out_l, float* out_r,
    int frames)                                                       { (void)h;(void)in_l;(void)in_r;(void)out_l;(void)out_r;(void)frames; return -1; }
static inline int ivanna_dsp_set_neuro_params(
    ivanna_dsp_handle_t h,
    float alpha, float beta, float gamma, float delta)               { (void)h;(void)alpha;(void)beta;(void)gamma;(void)delta; return -1; }
static inline int ivanna_dsp_get_metrics(
    ivanna_dsp_handle_t h,
    float* cpu_load, float* peak_amp)                                { (void)h;(void)cpu_load;(void)peak_amp; return -1; }

#ifdef __cplusplus
}
#endif
