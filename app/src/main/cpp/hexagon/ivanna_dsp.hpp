#pragma once
// ============================================================================
//  ivanna_dsp.hpp — DEPRECADO. No incluir desde código nuevo.
//
//  Este header solo contenía un struct huérfano (IvannaDspParams) sin ningún
//  consumidor en todo el repo. La API runtime real del DSP Hexagon vive en:
//      ivanna_dsp_rt.hpp            → API de bajo nivel (ivanna::hexagon::rt)
//      hexagon_dsp_integration.hpp  → fachada pública (ivanna::hexagon)
//
//  Se conserva únicamente para no romper includes históricos y redirige al
//  header canónico. Eliminar cuando se confirme que nada lo incluye.
// ============================================================================
#include "ivanna_dsp_rt.hpp"
