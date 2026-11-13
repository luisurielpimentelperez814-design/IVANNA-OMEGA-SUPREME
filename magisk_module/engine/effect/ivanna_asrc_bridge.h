// ============================================================================
// ivanna_asrc_bridge.h — LA MANO, tal como se enchufa al efecto existente.
// v2.5.396
//
// Punto de integración ÚNICO. En `omega_effect.cpp`, dentro de omega_process()
// (el callback de audio), en la PRIMERA línea, y UNA sola vez, se llama:
//
//     ivanna_asrc_bridge::process(buf_interleaved, frames, /*channels=*/2, /*rate=*/48000);
//
// REGLAS QUE ESTE BRIDGE CUMPLE POR CONSTRUCCIÓN (§5 y §4.4):
//   * Nada se inicializa en dlopen ni en omega_create_effect: la init es lazy,
//     en el primer bloque de audio, `not (std::nothrow)`, UN solo intento,
//     flag `attempted_`. Si falla -> passthrough para siempre. audioserver vivo.
//   * Sin I/O pesado: no se mapea nada (pread), no se abren HRTF, no hay mmap.
//   * Fail-closed: sin daemon, sin canal, bloque viejo, CRC malo, kill switch,
//     ratio fuera de riel o NaN -> ratio 1.0 -> memcpy bit-exacto.
//   * Todo acotado en tiempo: el Reader hace como máximo 3 pread de 64 B.
// ============================================================================
#ifndef IVANNA_ASRC_BRIDGE_H
#define IVANNA_ASRC_BRIDGE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace ivanna_asrc_bridge {

// Telemetría que el efecto publica hacia el daemon (canal inverso).
struct Telemetry {
    int64_t frames_written = 0;
    int64_t last_call_ns = 0;
    double applied_ratio = 1.0;
    bool passthrough = true;
    uint32_t ratio_updates = 0;
    uint32_t fallback_events = 0;   // cuántas veces no hubo ratio válido
};

// Procesa un bloque in-place. Devuelve los frames de salida producidos.
// `frames` es el número de frames de entrada disponibles en `buf`.
size_t process(int16_t* buf, size_t frames, int channels, int rate);

// Para la app / health_check: estado sin efectos secundarios.
Telemetry telemetry();
const char* lastReason();
void shutdown();          // cierre limpio (uninstall / kill switch)

// Sobrecarga de configuración para tests y para el daemon de pruebas.
void configure(const std::string& ratio_path,
               const std::string& frames_path,
               const std::string& kill_switch_path);

}  // namespace ivanna_asrc_bridge

#endif  // IVANNA_ASRC_BRIDGE_H
