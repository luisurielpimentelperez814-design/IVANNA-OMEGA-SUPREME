// IVANNA OMEGA SUPREME — Módulo de integridad anti-piratería (nativo).
//
// CONFIDENCIAL (interno): los detalles de este módulo NO se documentan en el
// README público ni en material promocional. Solo el usuario y su asistente
// conocen el diseño. Ver docs/SECURITY_INTERNAL.md (interno, no publicar).
//
// Propósito: hacer más difícil piratear el producto que crear uno nuevo.
// No es una garantía absoluta (ninguna comprobación on-device lo es contra
// un atacante con root y tiempo ilimitado); es defensa en profundidad que
// eleva el coste de: re-firmar el APK, reutilizar las .so en otro paquete,
// y distribuir copias modificadas.
//
// Componentes:
//   - Pin de certificado de firma generado en build-time (ivanna_build_pins.h,
//     generado por tools/security/generate_build_pins.py desde el keystore
//     que firma ESE build). Detecta re-firma: un APK re-firmado tiene otro
//     certificado y el pin no coincide.
//   - Self-check de ruta de carga (dladdr): la .so debe venir de una ruta
//     legítima (/data/app, /vendor, /system, /odm, /product). Detecta
//     sideload/inyección de una .so copiada a /data/local/tmp, /sdcard, etc.
//   - Gate de integridad (fail-closed): falso por defecto. Kotlin lo pone en
//     true solo después de verificar cert + vinculación. El DSP lo consulta;
//     si el gate no está verificado, el audio pasa en limpio (passthrough),
//     nunca se silencia y nunca se rompe el teléfono.
//
// Este módulo NO modifica FusionCore, HyperStateOperator, RealAcousticSensor
// ni el CMakeLists de tests. Se compila en libivanna_omega.so y en
// libomega_effect.so (ver app/src/main/cpp/CMakeLists.txt).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ivanna::security {

// Pin del certificado de firma (SHA-256 hex, minúsculas, sin ':').
// Generado en build-time; cadena vacía si el pin no fue generado.
const char* integrity_pinned_cert_sha256();

// Resultado de la verificación de certificado.
enum class CertCheck : int {
    kOk = 0,       // el certificado coincide con el pin de build
    kMismatch = 1, // el certificado NO coincide -> re-firma o modificación
    kNoPin = 2,    // no hay pin generado (build sin generateSecurityPins)
};

// Compara un SHA-256 hex (minúsculas, sin ':') con el pin de build.
// Comparación en tiempo constante. `hex` puede ser nulo -> kMismatch.
CertCheck integrity_verify_cert_sha256(const char* hex);

// Self-check: verifica que ESTA .so fue cargada desde una ruta legítima
// (/data/app, /vendor, /system, /odm, /product). El resultado se cachea
// (una sola vez por proceso); el coste por llamada posterior es un atomic
// load. Retorna false si la ruta es sospechosa (sideload/inyección).
bool integrity_self_check();

// Gate de integridad: false por defecto (fail-closed). Kotlin lo pone en
// true solo después de verificar cert + vinculación (ver IntegrityGate.kt).
// El DSP lo consulta antes de procesar.
bool integrity_gate_ok();
void integrity_set_gate(bool ok);
// Lockdown: gate=false. No rompe nada por sí mismo; solo marca el fallo.
void integrity_lockdown();

// SHA-256 autocontenido (sin dependencias externas).
// `out` debe tener al menos 32 bytes. Retorna false si `in` es nulo.
bool integrity_sha256(const uint8_t* in, size_t len, uint8_t out[32]);
// Hex (minúsculas, sin ':') de un digest SHA-256. `out`: 65 bytes (64 + NUL).
void integrity_sha256_hex(const uint8_t digest[32], char out[65]);

// SHA-256 hex de un archivo completo (lee por bloques de 64 KiB, sin
// cargarlo entero en memoria). `out_hex`: 65 bytes (64 + NUL). Retorna
// false si el archivo no se puede abrir o leer. Fase 2: verifica en runtime
// que la .so instalada coincide con el hash generado en build-time
// (detecta sustitución de la .so por una copia modificada con la misma
// firma aparente). En host funciona igual (misma función, sin Android).
bool integrity_sha256_file(const char* path, char out_hex[65]);

}  // namespace ivanna::security
