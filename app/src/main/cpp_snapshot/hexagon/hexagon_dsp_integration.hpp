#pragma once
// ============================================================================
// hexagon_dsp_integration.hpp — API pública de integración Hexagon DSP
// © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
//
// Este header sustituye al stub original. Expone un contrato mínimo estable
// para que el resto del pipeline (fastrpc_client, npe_engine, JNI) pueda:
//   - Consultar si el DSP Hexagon está disponible en el dispositivo.
//   - Obtener el nombre de la librería activa (cDSP / aDSP) para telemetría.
//   - Liberar recursos de forma explícita al apagar el motor.
//
// Toda la lógica real vive en ivanna_dsp.cpp (loader dlopen/dlsym).
// ============================================================================

#include <cstdint>

namespace ivanna { namespace hexagon {

// Fuerza la carga perezosa del loader FastRPC y retorna si quedó operativo.
// Idempotente y thread-safe. Devuelve false si el dispositivo no expone
// libcdsprpc.so ni libadsprpc.so, o si faltan símbolos IDL mínimos.
bool ensure_available() noexcept;

// Estado ya inicializado (no fuerza la carga).
bool is_available() noexcept;

// Nombre de la librería nativa cargada, o cadena vacía si ninguna. Solo
// para logging/telemetría — nunca dereferenciar como recurso.
const char* active_library() noexcept;

// Libera el handle dlopen. Segura de llamar múltiples veces y sin carga previa.
void release() noexcept;

}} // namespace ivanna::hexagon
