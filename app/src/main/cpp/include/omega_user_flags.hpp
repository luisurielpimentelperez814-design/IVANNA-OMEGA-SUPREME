#pragma once
/*
 * ============================================================
 * IVANNA — Canal de bits de USUARIO app -> daemon -> snapshot -> omega_effect (v2.5.16)
 *
 * PROBLEMA QUE RESUELVE: la app solo abre el bus de control como LECTOR (openReader), así que
 * OmegaControlBus::publish() desde el JNI devuelve false: los toggles Volterra H2, FastRPC cDSP y ATI
 * (y los controles del panel Atlas) solo cambiaban el estado del proceso de la app (Ruta A). La Ruta B
 * (omega_effect en audioserver, la de Spotify/YouTube) lee esos bits del snapshot que arma el daemon, y
 * el daemon reconstruía `flags` desde cero sin ellos: control muerto en el audio del sistema.
 *
 * CAMINO REAL (mismo patrón que SET_UPMIXING / SET_WFS):
 *   JNI -> socket abstracto "omega_command_socket" -> CommandServer (SET_USER_FLAGS)
 *       -> estado del daemon (mask/value) -> publishCurrentState() -> snapshot.flags -> omega_effect
 *
 * Este header tiene dos partes:
 *   - omega_user_flag_apply(): lógica PURA (sin I/O) que traduce clave+valor a máscara/valor de bits con
 *     TABLA BLANCA: solo estos bits son controlables por la app. Es lo que prueba el test de host.
 *   - omegaSendUserFlag(): cliente del socket (mejor esfuerzo, solo hilo UI, timeout 200 ms, nunca RT).
 * ============================================================
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include "omega_control_bus.h"

namespace ivanna {

// Valor "no tocar" (clave ausente en el JSON).
constexpr float OMEGA_USER_FLAG_UNSET = -9.0f;

struct OmegaUserFlagEntry { const char* key; uint32_t bit; };

// Claves del protocolo -> bit del snapshot. El valor enviado es el valor del BIT (1 = bit en alto).
// Los bits "_OFF" se envían ya invertidos por el llamador (p. ej. unityOff = 1 cuando el usuario apaga).
inline constexpr OmegaUserFlagEntry kOmegaUserFlagTable[] = {
    {"volterra",            OMEGA_FLAG_VOLTERRA_ON},
    {"fastrpc",             OMEGA_FLAG_FASTRPC_ON},
    {"ati",                 OMEGA_FLAG_ATI_ON},
    {"atlasSceneOff",       OMEGA_FLAG_ATLAS_SCENE_OFF},
    {"atlasLegacyDereverb", OMEGA_FLAG_ATLAS_LEGACY_DEREVERB},
    {"atlasLegacyEr",       OMEGA_FLAG_ATLAS_LEGACY_ER},
    {"atlasLegacyShaper",   OMEGA_FLAG_ATLAS_LEGACY_SHAPER},
    {"unityOff",            OMEGA_FLAG_UNITY_OFF},
};
inline constexpr const char* kOmegaUserFlagStyleKey = "atlasStyle";   // -1 = automático, 0..11 = arquetipo

// Codifica el arquetipo manual como lo decodifica omega_effect: bits 20..23 = (estilo + 1) & 0xF.
inline uint32_t omega_atlas_style_encode(int styleIdx) noexcept {
    return (styleIdx >= 0 && styleIdx < 12) ? ((static_cast<uint32_t>(styleIdx) + 1u) << OMEGA_FLAG_ATLAS_STYLE_SHIFT) : 0u;
}

// Aplica UNA clave del protocolo sobre (mask, value). Devuelve true si la clave pertenece a la tabla blanca.
// Un valor no finito o igual a OMEGA_USER_FLAG_UNSET no modifica nada (devuelve false).
inline bool omega_user_flag_apply(const char* key, float v, uint32_t& mask, uint32_t& value) noexcept {
    if (!key || !std::isfinite(v) || v <= OMEGA_USER_FLAG_UNSET + 0.5f) return false;
    if (std::strcmp(key, kOmegaUserFlagStyleKey) == 0) {
        mask  |= OMEGA_FLAG_ATLAS_STYLE_MASK;
        value  = (value & ~OMEGA_FLAG_ATLAS_STYLE_MASK) | omega_atlas_style_encode(static_cast<int>(std::lround(v)));
        return true;
    }
    for (const auto& e : kOmegaUserFlagTable) {
        if (std::strcmp(key, e.key) == 0) {
            mask |= e.bit;
            value = (v > 0.5f) ? (value | e.bit) : (value & ~e.bit);
            return true;
        }
    }
    return false;
}

// Los bits de usuario mezclados sobre los que arma el daemon: solo cambian los bits con máscara activa.
inline uint32_t omega_user_flags_merge(uint32_t daemonFlags, uint32_t mask, uint32_t value) noexcept {
    return (daemonFlags & ~mask) | (value & mask);
}

// Cliente genérico: envía UN comando JSON al socket del daemon. Mejor esfuerzo (timeout 200 ms por socket);
// SOLO hilo de UI, nunca el hilo de audio. Es el mismo transporte de SET_UPMIXING / SET_WFS / SET_AUX_FIELD.
inline void omegaSendJson(const char* json, size_t len) noexcept {
    if (!json || len == 0) return;
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";            // socket abstracto: sun_path[0] = '\0'
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    const socklen_t alen = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name));
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), alen) == 0) {
        const ssize_t w = ::write(fd, json, len);                          // mejor esfuerzo
        (void)w;
    }
    ::close(fd);
}

// Cliente: {"action":"SET_USER_FLAGS","<key>":<valor>}.
inline void omegaSendUserFlag(const char* key, int value) noexcept {
    if (!key) return;
    char json[160];
    const int n = std::snprintf(json, sizeof(json), "{\"action\":\"SET_USER_FLAGS\",\"%s\":%d}", key, value);
    if (n > 0 && n < static_cast<int>(sizeof(json))) omegaSendJson(json, static_cast<size_t>(n));
}

// Cliente: {"action":"SET_PUNCH","punchAmount":<0..1>} (impacto predictivo, Ruta B vía daemon).
inline void omegaSendPunch(float amount) noexcept {
    char json[96];
    const float a = (amount == amount) ? (amount < 0.f ? 0.f : (amount > 1.f ? 1.f : amount)) : 0.f;
    const int n = std::snprintf(json, sizeof(json), "{\"action\":\"SET_PUNCH\",\"punchAmount\":%.4f}", static_cast<double>(a));
    if (n > 0 && n < static_cast<int>(sizeof(json))) omegaSendJson(json, static_cast<size_t>(n));
}

} // namespace ivanna
