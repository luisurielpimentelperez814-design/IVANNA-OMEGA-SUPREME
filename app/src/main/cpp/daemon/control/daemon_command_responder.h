// daemon_command_responder.h — Servidor MÍNIMO de comandos del daemon v2.
//
// CAUSA RAÍZ (v2.5.440, verificado en vivo 2026-11-13): la app acopla el
// efecto omega con tryAttachDiscoveredSession → GET_STATUS por el socket
// ABSTRACTO @omega_daemon_socket. El daemon v2 (CEREBRO) bindeaba ese
// socket en bindDaemonSocket() pero NADIE aceptaba conexiones: main()
// guardaba el fd y el bucle principal lo drenaba con accept()+close()
// inmediato ("nadie debería conectar (no hay servidor de comandos)").
// El connect de la app entraba al backlog, GET_STATUS jamás se respondía,
// la app logueaba "GET_STATUS sin respuesta/JSON inválido" cada 3 s y el
// efecto nunca se creaba en audioserver (0 mapeos, asrc_frames.bin jamás
// existió, ivanna_asrcd en ratio 1.0). La reescritura a CEREBRO v2 (2.5.401)
// eliminó el CommandServer viejo y nunca se repuso.
//
// ESTE ARCHIVO lo repone en mínimo, fail-closed, SIN resucitar el
// CommandServer viejo (que arrastra shm_manager, snapshots, user flags y
// 800+ líneas de estado que el cerebro v2 no mantiene — dos escritores del
// mismo snapshot serían un riesgo mayor que el hueco que se cierra):
//   - Un hilo propio hace accept() sobre el socket abstracto YA bindeado
//     por el daemon; el bucle de fusión (~20 Hz) nunca se bloquea.
//   - Un comando por conexión, luego close (igual que el servidor viejo y
//     que el cliente de la app espera: exchange() abre conexión efímera
//     por comando).
//   - HELLO (handshake, proto >= 2) y GET_STATUS (sesión descubierta viva
//     + estado real del efecto por el beacon). Cualquier otra acción
//     responde {"ok":false,"error":"unsupported"}: el cerebro v2 NO aplica
//     comandos DSP (nunca lo hizo; antes ni respondía).
//   - Peer auth igual que el servidor viejo (peer_auth.hpp: root, shell
//     o el UID de la app). Sin ella, cualquier app podría preguntar.
//   - Conexión silenciosa (sin datos en 150 ms): se cierra sin responder.
//     La "Modo B" (fd por SCM_RIGHTS) NUNCA se implementó en el daemon
//     (ver shm_hyperplane.cpp: "nunca hubo sendmsg en el daemon") y ningún
//     cliente vivo la espera; el SHM viaja por backing file.
//   - Si el hilo muere por cualquier motivo, el proceso NO muere: la
//     fusión y el descubrimiento siguen (ratio 1.0, fail-closed).
//
// EL SOCKET ES EL ABSTRACTO (@omega_daemon_socket). No se crea ningún
// socket de archivo. /data/local/tmp/omega_effect_ctrl_local es OTRO
// canal (bus local de telemetría del efecto, lo escribe el efecto en
// audioserver) y no se toca aquí.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "include/ivanna_fusion_state.h"
#include "include/omega_effect_beacon.h"
#include "peer_auth.hpp"

namespace ivanna::daemon {

// Versión del protocolo de comandos (HELLO). Igual que OMEGA_PROTO_VERSION
// del CommandServer viejo (command_server.h). La app exige proto >= 2
// (MIN_PROTO_VERSION en OmegaEngineBridge.kt).
inline constexpr uint32_t kDaemonProtoVersion = 3u;

// Estado vivo que el hilo de comandos lee. El bucle principal del daemon
// lo actualiza cada iteración con stores atómicos; el hilo solo lee.
// (No es copiable por los atómicos: se pasa siempre por referencia.)
struct CommandPlaneState {
    std::atomic<int32_t> sessionId{0};
    std::atomic<uint64_t> sessionGen{0};
    std::atomic<uint64_t> generation{0};
    std::atomic<uint32_t> verdict{IVANNA_VERDICT_BYPASS};
    std::atomic<uint64_t> startMs{0};
    std::atomic<uint64_t> clientsServed{0};
};

// Texto de estado para GET_STATUS, derivado del veredicto del cerebro.
// (El beacon puede sobreescribirlo cuando el efecto procesa de verdad.)
inline const char* verdictStatus(uint32_t v) noexcept {
    switch (v) {
        case IVANNA_VERDICT_LOADED: return "loaded";
        case IVANNA_VERDICT_OFF: return "off";
        case IVANNA_VERDICT_NO_CLOCK: return "no_clock";
        case IVANNA_VERDICT_STALE: return "stale";
        case IVANNA_VERDICT_BYPASS: default: return "bypass";
    }
}

// Vista del estado del efecto, pura (testeable en host, sin I/O).
struct StatusView {
    int effectState;            // EffectBeaconState como int (-1..2)
    const char* status;         // texto para GET_STATUS
    const char* consumer;       // "omega_effect" o "null"
    uint64_t dspFrames;
    uint64_t dspAppliedGen;
};

// Mapeo beacon -> vista de estado. Regla del servidor viejo: el snapshot
// solo cuenta como aplicado cuando el efecto procesa Y su generación
// aplicada alcanza la generación publicada por el daemon.
inline StatusView computeStatusView(uint64_t generation, EffectBeaconState beacon,
                                    uint64_t dspFrames, uint64_t dspAppliedGen,
                                    uint32_t verdict) noexcept {
    StatusView v{};
    v.effectState = static_cast<int>(beacon);
    v.dspFrames = dspFrames;
    v.dspAppliedGen = dspAppliedGen;
    const bool processing = (beacon == EffectBeaconState::Processing);
    const bool idle = (beacon == EffectBeaconState::EnabledIdle);
    v.consumer = (processing || idle) ? "omega_effect" : "null";
    if (processing && generation > 0 && dspAppliedGen >= generation) {
        v.status = "dsp_block_applied";
    } else if (processing) {
        v.status = "awaiting_dsp_generation";
    } else if (idle) {
        v.status = "effect_enabled_idle";
    } else {
        v.status = verdictStatus(verdict);
    }
    return v;
}

// Extrae el valor de "action" de un JSON compacto (el que manda la app).
// No es un parser JSON general: el único cliente es OmegaEngineBridge,
// que manda {"action":"..."} compacto. Devuelve false si no hay acción.
inline bool extractAction(const std::string& req, std::string& action) noexcept {
    const std::size_t k = req.find("\"action\"");
    if (k == std::string::npos) return false;
    const std::size_t colon = req.find(':', k + 8);
    if (colon == std::string::npos) return false;
    const std::size_t q1 = req.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    const std::size_t q2 = req.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    action = req.substr(q1 + 1, q2 - q1 - 1);
    return !action.empty();
}

// Lee un número tras "key": en el JSON (para el proto del HELLO).
inline float jsonNumber(const std::string& req, const char* key, float dflt) noexcept {
    const std::string pat = std::string("\"") + key + "\"";
    const std::size_t k = req.find(pat);
    if (k == std::string::npos) return dflt;
    const std::size_t colon = req.find(':', k + pat.size());
    if (colon == std::string::npos) return dflt;
    return static_cast<float>(std::strtod(req.c_str() + colon + 1, nullptr));
}

// Respuesta HELLO. Misma forma que el servidor viejo (command, proto,
// compatible, status); sin shm_version/ctrl_version: el cerebro v2 no es
// dueño de esos layouts. La app solo exige proto >= 2.
inline std::string buildHelloReply(uint32_t clientProto) {
    const bool compatible = clientProto >= 1 && clientProto <= kDaemonProtoVersion;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"command\":\"HELLO\",\"proto\":%u,\"daemon\":\"ivanna_daemon_v2\","
        "\"compatible\":%s,\"status\":\"%s\"}",
        kDaemonProtoVersion,
        compatible ? "true" : "false",
        compatible ? "ready" : "proto_mismatch");
    return std::string(buf);
}

// Respuesta GET_STATUS. Campos que parseControlPlaneEvidence (Kotlin)
// exige: ok, generation, dsp_applied_generation, dsp_frames, effect_state,
// status, consumer, upmixing_enabled/immersivity, wfs_enabled/spread,
// discovered_session_id/gen. Los campos de efecto son REALES (beacon del
// efecto, no inventados): sin beacon, effect_state=-1 y status=veredicto.
// upmixing/wfs son 0.0 honestos: el cerebro v2 no los gestiona (los aplica
// el efecto desde el snapshot de la app, no desde el daemon).
inline std::string buildStatusReply(const CommandPlaneState& st, uint64_t nowMs,
                                    const char* beaconPath = OMEGA_EFFECT_BEACON_PATH) {
    const int32_t sid = st.sessionId.load(std::memory_order_relaxed);
    const uint64_t sgen = st.sessionGen.load(std::memory_order_relaxed);
    const uint64_t gen = st.generation.load(std::memory_order_relaxed);
    const uint32_t verdict = st.verdict.load(std::memory_order_relaxed);
    const uint64_t startMs = st.startMs.load(std::memory_order_relaxed);
    const uint64_t uptime = (nowMs >= startMs) ? (nowMs - startMs) : 0;
    const uint64_t clients = st.clientsServed.load(std::memory_order_relaxed);
    uint64_t dspFrames = 0, dspAppliedGen = 0;
    const EffectBeaconState beacon =
        readEffectBeacon(2000ull, beaconPath, &dspFrames, &dspAppliedGen);
    const StatusView view = computeStatusView(gen, beacon, dspFrames, dspAppliedGen, verdict);
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"command\":\"GET_STATUS\",\"status\":\"%s\",\"generation\":%llu,"
        "\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"effect_state\":%d,"
        "\"dsp_applied_generation\":%llu,\"dsp_frames\":%llu,\"error\":null,"
        "\"upmixing_enabled\":0.0,\"upmixing_immersivity\":0.0,"
        "\"wfs_enabled\":0.0,\"wfs_spread\":0.0,"
        "\"discovered_session_id\":%d,\"discovered_session_gen\":%llu,"
        "\"uptime_ms\":%llu,\"clients_served\":%llu}",
        view.status,
        (unsigned long long)gen,
        (std::strcmp(view.consumer, "omega_effect") == 0) ? "\"omega_effect\"" : "null",
        view.effectState,
        (unsigned long long)view.dspAppliedGen,
        (unsigned long long)view.dspFrames,
        static_cast<int>(sid), (unsigned long long)sgen,
        (unsigned long long)uptime, (unsigned long long)clients);
    return std::string(buf);
}

// Despacho de un comando (texto JSON ya recibido). Devuelve la respuesta,
// o cadena vacía si no hay nada que responder.
inline std::string handleCommand(const std::string& req, const CommandPlaneState& st) {
    std::string action;
    if (!extractAction(req, action)) return std::string("{\"ok\":false,\"error\":\"bad_request\"}");
    if (action == "HELLO") {
        const float proto = jsonNumber(req, "proto", 0.0f);
        const uint32_t p = (proto < 0.0f) ? 0u : static_cast<uint32_t>(proto);
        return buildHelloReply(p);
    }
    if (action == "GET_STATUS" || action == "STATUS") {
        return buildStatusReply(st, effectBeaconNowMs());
    }
    // El cerebro v2 no aplica comandos DSP (SET_*, PUSH_*, ...). Antes ni
    // respondía (timeout); ahora responde un error bien formado para que la
    // app no confunda "sin respuesta" con "daemon muerto".
    return std::string("{\"ok\":false,\"error\":\"unsupported\"}");
}

// Una conexión: auth, un recv (150 ms, igual que el servidor viejo), una
// respuesta, close. Sin datos en 150 ms se cierra sin responder (conexión
// silenciosa; ver nota Modo B arriba).
inline void handleCommandConnection(int cfd, const CommandPlaneState& st,
                                    bool enforcePeerAuth) noexcept {
    if (enforcePeerAuth && !ivanna::security::isAuthorizedControlPeer(cfd)) {
        ::close(cfd);
        return;
    }
    struct timeval rcvTv{0, 150000};
    ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcvTv, sizeof(rcvTv));
    char buf[4096] = {};
    const ssize_t n = ::recv(cfd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        const char* p = buf;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p == '{') {
            const std::string reply = handleCommand(std::string(p), st);
            if (!reply.empty()) {
                (void)::send(cfd, reply.data(), reply.size(), MSG_NOSIGNAL);
            }
        }
        // No-JSON: se cierra sin responder (fail-closed; el daemon v2 no
        // tiene protocolo de texto).
    }
    ::close(cfd);
}

// Bucle accept del hilo de comandos. Patrón del CommandServer viejo
// (select 1 s, un comando por conexión). Nunca bloquea el bucle de fusión:
// corre en su propio hilo. Si algo inesperado ocurre, el hilo muere y el
// proceso sigue (la fusión y el descubrimiento no dependen de él).
inline void serveCommandSocket(int listenFd, CommandPlaneState& st,
                               const std::atomic<bool>& running,
                               bool enforcePeerAuth) noexcept {
    try {
        while (running.load(std::memory_order_relaxed)) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listenFd, &rfds);
            struct timeval tv{1, 0};
            const int ret = ::select(listenFd + 1, &rfds, nullptr, nullptr, &tv);
            if (ret <= 0) continue;
            if (!FD_ISSET(listenFd, &rfds)) continue;
            const int cfd = ::accept(listenFd, nullptr, nullptr);
            if (cfd < 0) continue;
            st.clientsServed.fetch_add(1, std::memory_order_relaxed);
            handleCommandConnection(cfd, st, enforcePeerAuth);
        }
    } catch (...) {
        // El hilo de comandos nunca tumba el proceso.
    }
}

} // namespace ivanna::daemon
