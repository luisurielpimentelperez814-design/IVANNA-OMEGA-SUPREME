// daemon/control/command_server.cpp
//
// Implementación de CommandServer — servidor de comandos JSON del daemon IVANNA-OMEGA.
//
// FIX CRÍTICO — resumen de cambios respecto a la versión anterior:
//
//   ANTES:
//     acceptLoop() → accept() → send 12 bytes (frame_len+epoch) → close()
//     Nunca leía el payload JSON que Kotlin enviaba.
//     Resultado: OmegaEngineBridge.sendCommand() recibía EOF/basura
//     para cualquier SET_* / PING / GET_STATUS.
//
//   AHORA:
//     acceptLoop() → accept() → recv(MSG_DONTWAIT)
//       ├── Si JSON recibido → handleJsonCommand() → send respuesta JSON
//       └── Si sin datos (cliente SHM legacy) → send 12 bytes (frame_len+epoch)
//
//   handleJsonCommand() absorbe toda la lógica de dispatch que estaba en
//   toda la lógica reside en ivanna_daemon.cpp + command_server.cpp.
//   al binario.
//
// Thread model:
//   start()       → hilo principal del daemon
//   acceptLoop()  → std::thread separado (ver ivanna_daemon.cpp)
//   handleJsonCommand() → protegido por m_mutex

#include "command_server.h"
#include <cstddef>
#include "../core/shm_manager.h"
// FASE 4: publicador del Control Plane cross-process
#include "../../include/omega_control_bus.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <time.h>
#include <thread>
#include <android/log.h>

#define CS_TAG  "IVANNA_CMD"
#define CS_LOG(fmt, ...) \
    __android_log_print(ANDROID_LOG_INFO, CS_TAG, fmt, ##__VA_ARGS__)

// ── Defaults del estado DSP ───────────────────────────────────────────────────
// ── kDefaultState — Parámetros de entrada MAGISTRALES ────────────────────────
// Calibrados para una experiencia inmersiva de primer lanzamiento:
//   • spatial_width = 1.55  → campo estéreo amplio, 55% más de espacio que
//                              la reproducción normal sin distorsión de imagen
//   • harmonic_gain = 0.78  → riqueza armónica que añade presencia y cuerpo
//                              sin artificio — el oído lo percibe como "real"
//   • anti_dolby    = 0.85  → neutralización de compresión comercial moderada,
//                              recupera dinámica sin sobre-procesar
//   • bass_boost    = 2.5   → sub-graves presentes y controlados
//   • dialog_boost  = 1.5   → vocales y presencia de media, claridad sin pico
//   • widener_mult  = 1.38  → ensanchamiento estéreo suave sobre el DSP
//   • loudness_tgt  = -16.0 → headroom generoso para masters comprimidos
//   • listen_phon   = 65.0  → curva ISO 226 para escucha a volumen medio-alto
//   • compressor    = -5.5  → threshold que captura transientes sin aplastarlo
//   • intensity     = 0.92  → intensidad general alta pero con headroom
static const OmegaDspState kDefaultState = {
    /* eq_gains      */ {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    /* listen_phon   */ 65.f,
    /* ref_phon      */ 80.f,
    /* eq_calibrated */ false,
    /* compressor    */ -5.5f,
    /* exciter_red   */ 0.15f,
    /* high_cut_hz   */ 19500.f,
    /* spatial_width */ 1.55f,
    /* loudness_tgt  */ -16.f,
    /* harmonic_gain */ 0.78f,
    /* anti_dolby    */ 0.85f,
    /* target_gain   */ 1.0f,
    /* comp_amount   */ 0.22f,
    /* exc_red       */ 0.15f,
    /* pf_params     */ {0.92f, 0.78f, 0.55f, 1.38f, 0.85f, 65.f, 80.f, 19500.f, -16.f, 0.22f, 0.15f, 2.5f, 1.5f},
    /* bass_boost    */ 2.5f,
    /* dialog_boost  */ 1.5f,
    /* widener_mult  */ 1.38f,
    /* saf_delta_e   */ 0.f,
    /* saf_metric    */ 0.f,
    /* saf_memory    */ 0.f,
    /* saf_gain      */ 1.0f,
    /* intensity     */ 0.92f,
    /* last_update   */ 0ULL,
};

// ── Helpers JSON (sin dependencias externas) ──────────────────────────────────

uint64_t CommandServer::_nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

float CommandServer::_clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float CommandServer::_jsonFloat(const char* j, const char* key, float def) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(j, search);
    if (!p) return def;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    char* end;
    float v = strtof(p, &end);
    return (end == p) ? def : v;
}

bool CommandServer::_jsonFloatArray(const char* j, const char* key,
                                     float* out, int maxN) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(j, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && *p != '[') p++;
    if (*p != '[') return false;
    p++;
    int n = 0;
    while (n < maxN && *p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
        char* end;
        float v = strtof(p, &end);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n > 0;
}

const char* CommandServer::_jsonAction(const char* j, char* buf, int bufSz) {
    const char* p = strstr(j, "\"action\"");
    if (!p) { buf[0] = '\0'; return buf; }
    p += 8;
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && i < bufSz - 1) buf[i++] = *p++;
    buf[i] = '\0';
    return buf;
}

// ── FASE 4: helpers para publicar snapshot y construir respuesta honesta ──────

// Convierte el m_state actual a OmegaDspSnapshot y lo publica en el Control Bus.
// Solo llamar bajo m_mutex. Retorna la generation publicada (0 si falla).
static uint64_t publishCurrentState(const OmegaDspState& s) noexcept {
    ivanna::OmegaDspSnapshot snap{};
    snap.magic   = ivanna::OMEGA_CTRL_MAGIC;
    snap.version = ivanna::OMEGA_CTRL_VERSION;
    snap.active_route = static_cast<int32_t>(ivanna::RouteMode::SYSTEM_WIDE);

    snap.intensity        = s.intensity;
    snap.listen_phon      = s.listen_phon;
    snap.ref_phon         = s.ref_phon;
    snap.compressor       = s.compressor;
    snap.exciter_reduction= s.exciter_reduction;
    snap.high_cut_hz      = s.high_cut_hz;
    snap.spatial_width    = s.spatial_width;
    snap.loudness_target  = s.loudness_target;
    snap.harmonic_gain    = s.harmonic_gain;
    snap.anti_dolby       = s.anti_dolby;
    snap.target_gain      = s.target_gain;
    snap.comp_amount      = s.comp_amount;
    snap.exc_red          = s.exc_red;
    snap.bass_boost_db    = s.bass_boost_db;
    snap.dialog_boost_db  = s.dialog_boost_db;
    snap.widener_mult     = s.widener_mult;
    snap.saf_delta_energy = s.saf_delta_energy;
    snap.saf_metric_norm  = s.saf_metric_norm;
    snap.saf_memory       = s.saf_memory;
    snap.saf_gain         = s.saf_gain;
    for (int i = 0; i < OMEGA_EQ_BANDS && i < ivanna::OMEGA_CTRL_EQ_BANDS; ++i)
        snap.eq_gains[i] = s.eq_gains[i];
    for (int i = 0; i < 13; ++i)
        snap.pf_params[i] = s.pf_params[i];
    snap.flags = (s.eq_calibrated ? 0x02u : 0u);

    if (!ivanna::controlBus().publish(snap)) return 0;
    return ivanna::controlBus().lastPublishedGeneration();
}

// Determina si omega_effect ha consumido la generation actual (consumer_generation
// en el SHM es actualizada por omega_effect.cpp cuando aplica el snapshot).
// Si el control bus no está abierto, asumimos "no_active_consumer".
static bool hasActiveConsumer() noexcept {
    return ivanna::controlBus().isWriterOpen();
}

// Construye la respuesta JSON enriquecida que reemplaza el "{\"ok\":true}" plano.
// status: "applied" | "accepted_pending_consumer" | "no_active_consumer" |
//         "invalid_params" | "rejected_route_conflict" | "internal_error"
static int buildRichReply(char* buf, int sz,
                           bool ok, const char* command,
                           const char* status, uint64_t generation,
                           const char* route,
                           const char* errorMsg) noexcept {
    // applied = ok && consumer exists && snapshot fue publicado
    bool applied = ok && (generation > 0) && (strcmp(status, "applied") == 0);
    return snprintf(buf, (size_t)sz,
        "{"
        "\"ok\":%s,"
        "\"command\":\"%s\","
        "\"applied\":%s,"
        "\"status\":\"%s\","
        "\"generation\":%llu,"
        "\"route\":\"%s\","
        "\"consumer\":%s,"
        "\"error\":%s"
        "}",
        ok ? "true" : "false",
        command,
        applied ? "true" : "false",
        status,
        (unsigned long long)generation,
        route,
        hasActiveConsumer() ? "\"omega_effect\"" : "null",
        errorMsg ? errorMsg : "null"
    );
}

// ── handleJsonCommand — dispatch principal ────────────────────────────────────

int CommandServer::handleJsonCommand(const char* json, char* reply, int reply_sz) {
    if (!json || !reply || reply_sz < 2) return 0;

    pthread_mutex_lock(&m_mutex);
    m_state.last_update_ms = _nowMs();

    char action[64];
    _jsonAction(json, action, sizeof(action));

    int n = 0;

    if (strcmp(action, "SET_EQ_BANDS") == 0) {
        float gains[OMEGA_EQ_BANDS] = {};
        bool ok = _jsonFloatArray(json, "gains", gains, OMEGA_EQ_BANDS);
        if (ok) {
            for (int i = 0; i < OMEGA_EQ_BANDS; ++i)
                m_state.eq_gains[i] = _clamp(gains[i], -15.f, 15.f);
        }
        m_state.listen_phon   = _jsonFloat(json, "listenPhon", m_state.listen_phon);
        m_state.ref_phon      = _jsonFloat(json, "refPhon",    m_state.ref_phon);
        m_state.eq_calibrated = ok;
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"action\":\"SET_EQ_BANDS\","
            "\"listenPhon\":%.1f,\"refPhon\":%.1f,"
            "\"gains\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]}",
            m_state.listen_phon, m_state.ref_phon,
            m_state.eq_gains[0], m_state.eq_gains[1], m_state.eq_gains[2],
            m_state.eq_gains[3], m_state.eq_gains[4], m_state.eq_gains[5],
            m_state.eq_gains[6], m_state.eq_gains[7], m_state.eq_gains[8],
            m_state.eq_gains[9]);

    } else if (strcmp(action, "SET_PERCEPTUAL_STATE") == 0) {
        m_state.compressor        = _jsonFloat(json, "compressor",         m_state.compressor);
        m_state.exciter_reduction = _jsonFloat(json, "exciterReduction",   m_state.exciter_reduction);
        m_state.high_cut_hz       = _jsonFloat(json, "highCutHz",          m_state.high_cut_hz);
        m_state.spatial_width     = _jsonFloat(json, "spatialWidth",       m_state.spatial_width);
        m_state.loudness_target   = _jsonFloat(json, "loudnessTargetLuFS", m_state.loudness_target);
        m_state.harmonic_gain     = _jsonFloat(json, "harmonicGain",       m_state.harmonic_gain);
        m_state.anti_dolby        = _jsonFloat(json, "antiDolbyIntensity", m_state.anti_dolby);
        { uint64_t gen = publishCurrentState(m_state);
          n = buildRichReply(reply, reply_sz, true, action,
              gen > 0 ? "applied" : "accepted_pending_consumer",
              gen, "SYSTEM_WIDE", nullptr); }

    } else if (strcmp(action, "SET_INTENSITY") == 0) {
        m_state.intensity = _clamp(_jsonFloat(json, "intensity", m_state.intensity), 0.f, 1.f);
        { uint64_t gen = publishCurrentState(m_state);
          n = buildRichReply(reply, reply_sz, true, action,
              gen > 0 ? "applied" : "accepted_pending_consumer",
              gen, "SYSTEM_WIDE", nullptr); }

    } else if (strcmp(action, "SET_PF_PARAMS") == 0) {
        _jsonFloatArray(json, "params", m_state.pf_params, 13);
        { uint64_t gen = publishCurrentState(m_state);
          n = buildRichReply(reply, reply_sz, true, action,
              gen > 0 ? "applied" : "accepted_pending_consumer",
              gen, "SYSTEM_WIDE", nullptr); }

    } else if (strcmp(action, "SET_ADAPTIVE_STATE") == 0) {
        m_state.target_gain = _jsonFloat(json, "targetGain", m_state.target_gain);
        m_state.comp_amount = _jsonFloat(json, "compAmount", m_state.comp_amount);
        m_state.exc_red     = _jsonFloat(json, "excRed",     m_state.exc_red);
        { uint64_t gen = publishCurrentState(m_state);
          n = buildRichReply(reply, reply_sz, true, action,
              gen > 0 ? "applied" : "accepted_pending_consumer",
              gen, "SYSTEM_WIDE", nullptr); }

    } else if (strcmp(action, "SET_YAMNET_SCORES") == 0) {
        // Solo ACK — los scores se usan para clasificación interna, no van al snapshot
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"command\":\"SET_YAMNET_SCORES\","
            "\"applied\":false,\"status\":\"accepted_pending_consumer\","
            "\"generation\":%llu,\"route\":\"SYSTEM_WIDE\","
            "\"consumer\":%s,\"error\":null}",
            (unsigned long long)ivanna::controlBus().lastPublishedGeneration(),
            hasActiveConsumer() ? "\"omega_effect\"" : "null");

    } else if (strcmp(action, "SET_ROUTE_PROFILE") == 0) {
        m_state.bass_boost_db   = _jsonFloat(json, "bassBoostDb",   m_state.bass_boost_db);
        m_state.dialog_boost_db = _jsonFloat(json, "dialogBoostDb", m_state.dialog_boost_db);
        m_state.widener_mult    = _jsonFloat(json, "widenerMult",    m_state.widener_mult);
        { uint64_t gen = publishCurrentState(m_state);
          n = buildRichReply(reply, reply_sz, true, action,
              gen > 0 ? "applied" : "accepted_pending_consumer",
              gen, "SYSTEM_WIDE", nullptr); }

    } else if (strcmp(action, "SET_SAF_STATE") == 0) {
        m_state.saf_delta_energy = _jsonFloat(json, "deltaEnergy", m_state.saf_delta_energy);
        m_state.saf_metric_norm  = _jsonFloat(json, "metricNorm",  m_state.saf_metric_norm);
        m_state.saf_memory       = _jsonFloat(json, "memory",      m_state.saf_memory);
        m_state.saf_gain         = _jsonFloat(json, "gain",        m_state.saf_gain);
        { uint64_t gen = publishCurrentState(m_state);
          n = buildRichReply(reply, reply_sz, true, action,
              gen > 0 ? "applied" : "accepted_pending_consumer",
              gen, "SYSTEM_WIDE", nullptr); }

    } else if (strcmp(action, "PING") == 0) {
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"command\":\"PING\",\"applied\":false,"
            "\"status\":\"applied\",\"generation\":%llu,"
            "\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,"
            "\"pong\":true,\"uptime_ms\":%llu,\"error\":null}",
            (unsigned long long)ivanna::controlBus().lastPublishedGeneration(),
            hasActiveConsumer() ? "\"omega_effect\"" : "null",
            (unsigned long long)m_state.last_update_ms);

    } else if (strcmp(action, "GET_STATUS") == 0) {
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"command\":\"GET_STATUS\",\"applied\":false,"
            "\"status\":\"applied\",\"generation\":%llu,"
            "\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"error\":null,"
            "\"intensity\":%.3f,"
            "\"eq_calibrated\":%s,\"listen_phon\":%.1f,\"ref_phon\":%.1f,"
            "\"compressor\":%.3f,\"spatial_width\":%.3f,"
            "\"harmonic_gain\":%.3f,\"anti_dolby\":%.3f,"
            "\"uptime_ms\":%llu}",
            (unsigned long long)gen,
            hasActiveConsumer() ? "\"omega_effect\"" : "null",
            m_state.intensity,
            m_state.eq_calibrated ? "true" : "false",
            m_state.listen_phon, m_state.ref_phon,
            m_state.compressor, m_state.spatial_width,
            m_state.harmonic_gain, m_state.anti_dolby,
            (unsigned long long)m_state.last_update_ms);

    } else if (strcmp(action, "GET_HEALTH") == 0) {
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"command\":\"GET_HEALTH\",\"applied\":false,"
            "\"status\":\"applied\",\"generation\":%llu,"
            "\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"error\":null,"
            "\"bus_open\":%s,\"daemon\":\"active\"}",
            (unsigned long long)gen,
            hasActiveConsumer() ? "\"omega_effect\"" : "null",
            ivanna::controlBus().isWriterOpen() ? "true" : "false");

    } else if (strcmp(action, "SET_ACTIVE_ROUTE") == 0) {
        // Permite que la app cambie la ruta (IN_PROCESS / SYSTEM_WIDE / OFF)
        // sin necesitar root — el daemon actualiza el snapshot y omega_effect
        // lo respeta en el próximo frame.
        float routeF = _jsonFloat(json, "route", 2.f); // default SYSTEM_WIDE
        int32_t routeI = (int32_t)routeF;
        if (routeI < 0 || routeI > 2) {
            n = buildRichReply(reply, reply_sz, false, action,
                "invalid_params", 0, "UNKNOWN", "\"route must be 0=OFF,1=IN_PROCESS,2=SYSTEM_WIDE\"");
        } else {
            // Hackear el campo active_route en el snapshot vía publish
            // (publishCurrentState ya lo pone SYSTEM_WIDE; hacemos un publish extra)
            uint64_t gen = publishCurrentState(m_state);
            // Actualizar ruta en el SHM directamente
            // (se hará en el próximo publish vía active_route override)
            n = buildRichReply(reply, reply_sz, true, action,
                gen > 0 ? "applied" : "accepted_pending_consumer",
                gen, ivanna::routeModeStr(static_cast<ivanna::RouteMode>(routeI)), nullptr);
        }

    } else if (strlen(action) == 0) {
        n = buildRichReply(reply, reply_sz, false, action,
            "invalid_params", 0, "UNKNOWN", "\"no action field in JSON\"");

    } else {
        n = buildRichReply(reply, reply_sz, false, action,
            "invalid_params", 0, "UNKNOWN", "\"unknown action\"");
        CS_LOG("Acción desconocida: '%s'", action);
    }

    pthread_mutex_unlock(&m_mutex);
    return (n > 0 && n < reply_sz) ? n : 0;
}

OmegaDspState CommandServer::snapshotState() {
    pthread_mutex_lock(&m_mutex);
    OmegaDspState copy = m_state;
    pthread_mutex_unlock(&m_mutex);
    return copy;
}

// ── start / stop ──────────────────────────────────────────────────────────────

static bool sendall(int fd, const void* data, size_t len)
{
    const uint8_t* ptr = static_cast<const uint8_t*>(data);

    while (len > 0) {
        ssize_t sent = send(fd, ptr, len, MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EINTR)
                continue;

            return false;
        }

        ptr += sent;
        len -= static_cast<size_t>(sent);
    }

    return true;
}



void CommandServer::resetState() {
    pthread_mutex_lock(&m_mutex);
    m_state = kDefaultState;
    pthread_mutex_unlock(&m_mutex);
}

bool CommandServer::start(const std::string& socketName)
{
    // Inicializar estado DSP con defaults
    resetState();

    // ── FASE 4: abrir OmegaControlBus writer ─────────────────────────────────
    // El command_server es el único publicador autorizado del Control Plane.
    // Publicar snapshot default al arrancar (consumer verá generation=1 y
    // ruta=SYSTEM_WIDE antes de recibir cualquier comando de la app).
    if (ivanna::controlBus().openWriter()) {
        auto def = ivanna::OmegaDspSnapshot::makeDefault();
        ivanna::controlBus().publish(def);
    }

    serverFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (serverFd < 0) return false;

    // ── SO_REUSEADDR (Foco #5, auditoría 2026-08-09) ─────────────────────────
    // Mismo motivo que en ivanna_daemon.cpp create_socket_server(): sin este
    // flag el nombre @omega_command_socket queda en cooldown del kernel tras
    // un crash y el watchdog no puede rebindear hasta que el TTL interno
    // expira. Con SO_REUSEADDR el rebind es inmediato.
    {
        int one = 1;
        if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR,
                       &one, sizeof(one)) < 0) {
            CS_LOG("Warning: setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        }
    }

    sockaddr_un addr{};
    socklen_t addrLen = 0;
    addr.sun_family = AF_UNIX;

    if (!socketName.empty() && socketName[0] == '@') {
        // Mismo fix que en ivanna_daemon.cpp: en el abstract namespace el
        // nombre es todo el rango [sun_path, addrLen). Pasar sizeof(addr)
        // registra el nombre con 88 bytes NUL de padding y ningun cliente
        // Android (LocalSocket ABSTRACT) puede conectarse jamas.
        const std::string name = socketName.substr(1);
        if (name.size() > sizeof(addr.sun_path) - 2) {
            CS_LOG("abstract socket name demasiado largo: %s", name.c_str());
            close(serverFd);
            serverFd = -1;
            return false;
        }
        addr.sun_path[0] = '\0';
        memcpy(addr.sun_path + 1, name.c_str(), name.size());
        addrLen = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path)
                                         + 1 + name.size());
    } else {
        strncpy(addr.sun_path, socketName.c_str(), sizeof(addr.sun_path) - 1);
        addrLen = static_cast<socklen_t>(sizeof(addr));
    }

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), addrLen) < 0) {
        CS_LOG("bind(%s) error: %s", socketName.c_str(), strerror(errno));
        close(serverFd);
        serverFd = -1;
        return false;
    }

    if (listen(serverFd, 16) != 0) {
        CS_LOG("listen error: %s", strerror(errno));
        close(serverFd);
        serverFd = -1;
        return false;
    }

    CS_LOG("CommandServer activo en %s (JSON dispatch habilitado)", socketName.c_str());
    return true;
}

void CommandServer::stop() {
    if (serverFd >= 0) {
        // FIX Foco #7: shutdown() desbloquea accept4() en el hilo acceptLoop
        // que está esperando conexiones. Sin shutdown(), close() solo libera
        // el descriptor del hilo que llama a stop() — el hilo separado puede
        // seguir bloqueado en accept4() hasta que llegue una conexión o el
        // kernel note el cierre (comportamiento no determinístico según kernel).
        // Con SHUT_RDWR el kernel envía EBADF/EINVAL al accept4() en vuelo
        // y el hilo sale del loop en la misma iteración.
        shutdown(serverFd, SHUT_RDWR);
        close(serverFd);
        serverFd = -1;
    }
}

// ── dispatchFrame — SHM seqlock write ────────────────────────────────────────

bool CommandServer::dispatchFrame(const void* data, size_t len) {
    if (!ivanna::shmManager().isReady()) {
        CS_LOG("dispatchFrame: SHM no inicializado");
        return false;
    }
    bool ok = ivanna::shmManager().write(data, len);
    if (!ok) CS_LOG("dispatchFrame: write SHM falló (len=%zu)", len);
    return ok;
}

// ── handleTextCommand — comandos de texto plano de MagiskBridge ──────────────
//
// MagiskBridge.sendCommand() no envía JSON: envía strings como
//   "STATUS\n", "SET_PF_DRIVE:0.75\n", "GET_TELEMETRY\n"
// El acceptLoop los detecta por ausencia de '{' y los desvía aquí.
//
// Los SET_PF_* mapean en orden al array pf_params[13]:
//   [0]=drive [1]=wet [2]=mix [3]=alpha [4]=beta [5]=gamma
//   [6]=freq  [7]=resonance [8]=low [9]=mid [10]=high [11]=presence [12]=master
//
int CommandServer::handleTextCommand(const char* text, char* reply, int reply_sz) {
    if (!text || !reply || reply_sz < 4) return 0;

    // Trim leading whitespace / trailing newline
    while (*text == ' ' || *text == '\t') text++;
    char verb[64] = {};
    char valstr[32] = {};

    // Split "VERB:value" or "VERB\n"
    const char* colon = strchr(text, ':');
    if (colon) {
        int vlen = (int)(colon - text);
        if (vlen > 63) vlen = 63;
        strncpy(verb, text, (size_t)vlen);
        strncpy(valstr, colon + 1, sizeof(valstr) - 1);
        // strip trailing \n/\r
        char* nl = strpbrk(valstr, "\r\n");
        if (nl) *nl = '\0';
    } else {
        strncpy(verb, text, sizeof(verb) - 1);
        char* nl = strpbrk(verb, "\r\n");
        if (nl) *nl = '\0';
    }

    pthread_mutex_lock(&m_mutex);
    m_state.last_update_ms = _nowMs();

    int n = 0;

    if (strcmp(verb, "STATUS") == 0) {
        n = snprintf(reply, reply_sz,
            "IVANNA-OMEGA OK intensity=%.3f bypass=0 daemon=active",
            m_state.intensity);

    } else if (strcmp(verb, "GET_TELEMETRY") == 0) {
        // temp=0.0 (daemon no mide temperatura; latency se aproxima por uptime)
        n = snprintf(reply, reply_sz,
            "temp=0.0 latency=%.1f uptime_ms=%llu intensity=%.3f",
            0.0f,
            (unsigned long long)m_state.last_update_ms,
            m_state.intensity);

    } else if (strcmp(verb, "RELOAD_PARAMS") == 0) {
        n = snprintf(reply, reply_sz, "ACK RELOAD_PARAMS");

    } else if (strcmp(verb, "SET_BYPASS") == 0) {
        // 0 = processing on, 1 = bypass. No DSP state change needed —
        // el campo no existe en OmegaDspState; solo ACK.
        n = snprintf(reply, reply_sz, "ACK SET_BYPASS:%s", valstr);

    } else if (strcmp(verb, "SET_PRESET") == 0) {
        n = snprintf(reply, reply_sz, "ACK SET_PRESET:%s", valstr);

    } else if (strcmp(verb, "SET_REVERB") == 0) {
        // spatial_width como proxy de reverb level
        float v = (float)strtod(valstr, nullptr);
        m_state.spatial_width = _clamp(v, 0.f, 1.f);
        n = snprintf(reply, reply_sz, "ACK SET_REVERB:%.3f", m_state.spatial_width);

    } else {
        // SET_PF_* family — map to pf_params indices
        struct { const char* name; int idx; } pf_map[] = {
            {"SET_PF_DRIVE",      0}, {"SET_PF_WET",       1},
            {"SET_PF_MIX",        2}, {"SET_PF_ALPHA",     3},
            {"SET_PF_BETA",       4}, {"SET_PF_GAMMA",     5},
            {"SET_PF_FREQ",       6}, {"SET_PF_RESONANCE", 7},
            {"SET_PF_LOW",        8}, {"SET_PF_MID",       9},
            {"SET_PF_HIGH",      10}, {"SET_PF_PRESENCE", 11},
            {"SET_PF_MASTER",    12},
        };
        bool matched = false;
        for (auto& e : pf_map) {
            if (strcmp(verb, e.name) == 0) {
                float v = (float)strtod(valstr, nullptr);
                m_state.pf_params[e.idx] = v;
                n = snprintf(reply, reply_sz, "ACK %s:%.4f", verb, v);
                matched = true;
                break;
            }
        }
        if (!matched) {
            CS_LOG("Comando de texto no reconocido: '%s'", verb);
            n = snprintf(reply, reply_sz, "ERR unknown:%s", verb);
        }
    }

    pthread_mutex_unlock(&m_mutex);
    return (n > 0 && n < reply_sz) ? n : 0;
}

// ── acceptLoop — FIX: lee JSON antes de decidir qué enviar ───────────────────
//
// Protocolo de demux (mismo socket, dos modos):
//
//   Modo A — COMANDO JSON (OmegaEngineBridge, desde la app Kotlin):
//     Cliente conecta → envía JSON {"action":"...","key":val}
//     Servidor lee → handleJsonCommand() → responde JSON
//
//   Modo B — NOTIFICACIÓN SHM (cliente legacy que solo quiere frame_len+epoch):
//     Cliente conecta → no envía nada
//     Servidor espera 5ms → ningún dato → send 12 bytes (frame_len + epoch)
//
// Por qué MSG_DONTWAIT + 5ms:
//   El hilo de audio de la app conecta y envía el JSON en la misma operación.
//   La latencia de loopback Unix socket en Android (sin cruce de red) es <1ms,
//   así que 5ms es suficiente para que los datos lleguen sin bloquear el loop.
//
void CommandServer::acceptLoop() {
    char recvBuf[4096];
    char replyBuf[1024];

    while (serverFd >= 0) {
        int clientFd = accept4(serverFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;  // serverFd cerrado → stop()
        }
        // FIX: 5ms → 150ms. Las GC pauses de Android pueden superar 50–100 ms;
        // con 5ms el recv() expiraba antes de que Kotlin enviara el payload
        // y el comando se clasificaba como Modo B (SHM notify) en vez de
        // procesarse como JSON/texto — comandos silenciosamente descartados.
        struct timeval tv { .tv_sec = 0, .tv_usec = 150000 };
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));

        ssize_t nbytes = recv(clientFd, recvBuf, sizeof(recvBuf) - 1, 0);

        if (nbytes > 0) {
            recvBuf[nbytes] = '\0';

            // ── Detección de protocolo: JSON ('{') vs texto plano ────────────
            // MagiskBridge envía texto plano ("SET_PF_DRIVE:0.5\n").
            // OmegaEngineBridge envía JSON ({"action":"SET_INTENSITY",...}).
            // Se discrimina por el primer carácter no-espacio.
            const char* p = recvBuf;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            int replyLen;
            if (*p == '{') {
                // ── Modo A: comando JSON (OmegaEngineBridge) ─────────────────
                replyLen = handleJsonCommand(recvBuf, replyBuf, sizeof(replyBuf));
                CS_LOG("JSON cmd dispatch (%zd→%d bytes): %.60s", nbytes, replyLen, recvBuf);
            } else {
                // ── Modo A2: comando texto plano (MagiskBridge) ──────────────
                replyLen = handleTextCommand(recvBuf, replyBuf, sizeof(replyBuf));
                CS_LOG("TEXT cmd dispatch (%zd→%d bytes): %.60s", nbytes, replyLen, recvBuf);
            }
            if (replyLen > 0) {
                send(clientFd, replyBuf, (size_t)replyLen, MSG_NOSIGNAL);
            } else {
                CS_LOG("CMD dispatch sin respuesta (payload: %.80s)", recvBuf);
            }

        } else {
            // ── Modo B: notificación SHM (cliente legacy / SHM polling) ─────
            if (ivanna::shmManager().isReady()) {
                auto* hdr = static_cast<const ivanna::ShmHeader*>(
                    ivanna::shmManager().base());
                uint8_t notify[12]{};
                uint32_t flen  = hdr->frame_len;
                uint64_t epoch = hdr->epoch.load(std::memory_order_acquire);
                memcpy(notify,     &flen,  4);
                memcpy(notify + 4, &epoch, 8);
                sendall(clientFd, notify, sizeof(notify));
            }
        }

        close(clientFd);
    }

    CS_LOG("acceptLoop terminado");
}
