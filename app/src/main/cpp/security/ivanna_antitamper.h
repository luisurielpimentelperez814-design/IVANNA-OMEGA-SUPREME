// IVANNA OMEGA SUPREME — Anti-debug / anti-tamper / anti-hook.
// CONFIDENCIAL (interno). Ver docs/SECURITY_INTERNAL.md.
//
// Detecta (todo on-device, sin servidor):
//   - Depurador: TracerPid de /proc/self/status (!= 0 -> un depurador está
//     attached por ptrace). No se usa el truco PTRACE_TRACEME: TracerPid ya
//     detecta cualquier attach por ptrace, y el self-ptrace marca el proceso
//     como trazable por su padre, lo que puede interferir con el manejo de
//     crashes del sistema (debuggerd) en audioserver.
//   - Frida: artefactos en /proc/self/maps ("frida", "gadget") o archivos
//     conocidos del servidor Frida.
//   - Frameworks de hooks: Xposed / LSPosed / EdXposed / Substrate / Cydia
//     en /proc/self/maps. NO se marca Zygisk ni Riru: son de Magisk y el
//     producto está diseñado para dispositivos con Magisk (falso positivo
//     seguro en el teléfono del usuario).
//   - Emulador: propiedades del sistema (goldfish/ranchu/generic/sdk) y
//     /dev/qemu_pipe.
//   - Root: binarios su y archivos de Magisk/Superuser. SOLO señal
//     informativa: el producto está diseñado para dispositivos rooteados
//     (módulo Magisk + daemon como root), así que root por sí solo NO
//     dispara lockdown. En el teléfono del usuario esta señal estará
//     presente y es normal.
//
// Lockdown: depurador, Frida, framework de hooks o emulador -> el DSP no
// procesa (passthrough, audio sigue fluyendo). Root solo -> se registra,
// no bloquea. En host (no Android) todas las comprobaciones devuelven
// "no detectado" (los tests host pasan y el código es seguro fuera del
// dispositivo).
//
// Coste: la evaluación corre una sola vez por proceso (resultado cacheado);
// las llamadas posteriores son un atomic load. No va en el hot path de
// audio (se evalúa en IntegrityGate.verify() y una vez por proceso en
// omega_process, junto al self-check ya cacheado).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ivanna::security {

// Señales de manipulación detectadas (bitmask).
inline constexpr std::uint32_t kAntiTamperDebugger = 1u << 0;       // TracerPid != 0
inline constexpr std::uint32_t kAntiTamperFrida = 1u << 1;           // Frida en maps/archivos
inline constexpr std::uint32_t kAntiTamperHookFramework = 1u << 2;   // Xposed/LSPosed/Substrate/Cydia
inline constexpr std::uint32_t kAntiTamperEmulator = 1u << 3;        // emulador
inline constexpr std::uint32_t kAntiTamperRoot = 1u << 4;            // su/Magisk (SOLO señal)

// Disparadores de lockdown. Root NO está incluido (ver comentario arriba).
inline constexpr std::uint32_t kAntiTamperLockdownMask =
    kAntiTamperDebugger | kAntiTamperFrida | kAntiTamperHookFramework |
    kAntiTamperEmulator;

// Ejecuta todas las comprobaciones una sola vez (resultado cacheado) y
// devuelve el bitmask de señales. En host (no Android) devuelve 0.
std::uint32_t antitamper_evaluate();

// True si hay alguna señal que dispara lockdown.
bool antitamper_should_lockdown();

// Reinicia la caché (solo para tests).
void antitamper_reset_for_tests();

// ── Helpers testeables en host (no dependen de Android) ────────────────────
// Parsea el valor de TracerPid de un contenido de /proc/self/status.
// Retorna -1 si no se encuentra el campo.
int antitamper_parse_tracer_pid(const char* status_text);

// True si `value` contiene un marcador de emulador (case-insensitive).
bool antitamper_is_emulator_marker(const char* value);

// Escanea un texto (contenido de /proc/self/maps) buscando artefactos de
// Frida / frameworks de hooks (case-insensitive). Retorna el bitmask de
// categorías encontradas (kAntiTamperFrida y/o kAntiTamperHookFramework).
std::uint32_t antitamper_scan_maps_text(const char* text);

// Búsqueda de subcadena case-insensitive. Retorna false si algún puntero
// es nulo o la aguja está vacía.
bool antitamper_contains_ci(const char* haystack, const char* needle);

}  // namespace ivanna::security
