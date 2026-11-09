// IVANNA OMEGA SUPREME — Verificación de integridad del efecto stageado.
// CONFIDENCIAL (interno). Ver docs/SECURITY_INTERNAL.md.
//
// El daemon (root, vía Magisk) verifica en arranque que la libomega_effect.so
// stageada en /vendor coincida con el hash generado en build-time (CI) y
// guardado en el módulo (ivanna_effect_hash.txt, junto a la .so). Detecta
// la sustitución de la .so del módulo por una copia modificada.
//
// Comportamiento:
//   - kOk: el hash coincide -> el daemon puede pilotar el efecto.
//   - kMismatch: el hash NO coincide (o el archivo de hash está corrupto)
//     -> el daemon NO aplica parámetros DSP (SET_*) y lo registra en voz
//     alta (logcat + daemon.log + archivo de estado). El efecto queda en
//     sus valores por defecto (passthrough). Las consultas/telemetría siguen.
//   - kNoHashFile: el módulo no trae el archivo de hash (módulo anterior
//     a esta función) -> aviso y se continúa (compatibilidad hacia atrás).
//     NO bloquea: los módulos viejos no tienen el archivo.
//   - kNoEffectFile / kIoError: la .so no está o no se puede leer -> el
//     efecto no está activo de todos modos; no se pilotan parámetros.
//
// La verificación corre una sola vez por arranque del daemon (resultado
// cacheado). El hash se calcula con el SHA-256 autocontenido del módulo
// de integridad (misma implementación que el pin de certificado).
#pragma once

#include <cstdint>

namespace ivanna::security {

// Ruta de la .so del efecto stageada (overlay de Magisk sobre /vendor).
inline constexpr const char* kEffectSoPath =
    "/vendor/lib64/soundfx/libomega_effect.so";
// Hash esperado, generado por CI tras stagear la .so (ver build.yml).
inline constexpr const char* kEffectHashPath =
    "/vendor/lib64/soundfx/ivanna_effect_hash.txt";
// Archivo de estado (legible por la app) con el resultado de la verificación.
inline constexpr const char* kEffectIntegrityStatusPath =
    "/data/adb/ivanna_omega/effect_integrity.status";

enum class EffectIntegrity : int {
    kOk = 0,          // hash coincide
    kMismatch = 1,    // hash NO coincide (o hash esperado corrupto)
    kNoHashFile = 2,  // el módulo no trae el archivo de hash (módulo viejo)
    kNoEffectFile = 3,  // la .so no está (efecto no stageado)
    kIoError = 4,     // no se pudo leer/hashear la .so
};

// Verifica la .so stageada contra el hash del módulo. Corre una sola vez
// por proceso (resultado cacheado); las llamadas posteriores devuelven el
// mismo resultado. Escribe el resultado en el archivo de estado.
EffectIntegrity effect_integrity_verify();

// True si el daemon puede pilotar el efecto (kOk o kNoHashFile).
// False en kMismatch, kNoEffectFile y kIoError.
bool effect_integrity_ok();

// Texto corto del resultado ("ok", "mismatch", "no_hash_file",
// "no_effect_file", "io_error"). Para logs y el archivo de estado.
const char* effect_integrity_status_string(EffectIntegrity result);

}  // namespace ivanna::security
