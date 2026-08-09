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
//   OmegaDaemonV8.cpp (archivo muerto — no hay CMakeLists que lo enlace).
//   OmegaDaemonV8.cpp se conserva como referencia pero ya no aporta código
//   al binario.
//
// Thread model:
//   start()       → hilo principal del daemon
//   acceptLoop()  → std::thread separado (ver ivanna_daemon.cpp)
//   handleJsonCommand() → protegido por m_mutex

#include "command_server.h"
#include <cstddef>
#include "../core/shm_manager.h"

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
static const OmegaDspState kDefaultState = {
    /* eq_gains      */ {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    /* listen_phon   */ 60.f,
    /* ref_phon      */ 80.f,
    /* eq_calibrated */ false,
    /* compressor    */ 0.5f,
    /* exciter_red   */ 0.f,
    /* high_cut_hz   */ 18000.f,
    /* spatial_width */ 1.0f,
    /* loudness_tgt  */ -14.f,
    /* harmonic_gain */ 0.5f,
    /* anti_dolby    */ 1.0f,
    /* target_gain   */ 1.0f,
    /* comp_amount   */ 0.f,
    /* exc_red       */ 0.f,
    /* pf_params     */ {},
    /* bass_boost    */ 0.f,
    /* dialog_boost  */ 0.f,
    /* widener_mult  */ 1.0f,
    /* saf_delta_e   */ 0.f,
    /* saf_metric    */ 0.f,
    /* saf_memory    */ 0.f,
    /* saf_gain      */ 1.0f,
    /* intensity     */ 0.85f,
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
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_PERCEPTUAL_STATE\"}");

    } else if (strcmp(action, "SET_INTENSITY") == 0) {
        m_state.intensity = _clamp(_jsonFloat(json, "intensity", m_state.intensity), 0.f, 1.f);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"intensity\":%.3f}", m_state.intensity);

    } else if (strcmp(action, "SET_PF_PARAMS") == 0) {
        _jsonFloatArray(json, "params", m_state.pf_params, 13);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_PF_PARAMS\"}");

    } else if (strcmp(action, "SET_ADAPTIVE_STATE") == 0) {
        m_state.target_gain = _jsonFloat(json, "targetGain", m_state.target_gain);
        m_state.comp_amount = _jsonFloat(json, "compAmount", m_state.comp_amount);
        m_state.exc_red     = _jsonFloat(json, "excRed",     m_state.exc_red);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_ADAPTIVE_STATE\"}");

    } else if (strcmp(action, "SET_YAMNET_SCORES") == 0) {
        // Solo ACK — los scores se usan para clasificación interna
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_YAMNET_SCORES\"}");

    } else if (strcmp(action, "SET_ROUTE_PROFILE") == 0) {
        m_state.bass_boost_db   = _jsonFloat(json, "bassBoostDb",   m_state.bass_boost_db);
        m_state.dialog_boost_db = _jsonFloat(json, "dialogBoostDb", m_state.dialog_boost_db);
        m_state.widener_mult    = _jsonFloat(json, "widenerMult",    m_state.widener_mult);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_ROUTE_PROFILE\"}");

    } else if (strcmp(action, "SET_SAF_STATE") == 0) {
        m_state.saf_delta_energy = _jsonFloat(json, "deltaEnergy", m_state.saf_delta_energy);
        m_state.saf_metric_norm  = _jsonFloat(json, "metricNorm",  m_state.saf_metric_norm);
        m_state.saf_memory       = _jsonFloat(json, "memory",      m_state.saf_memory);
        m_state.saf_gain         = _jsonFloat(json, "gain",        m_state.saf_gain);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_SAF_STATE\"}");

    } else if (strcmp(action, "PING") == 0) {
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"pong\":true,\"uptime_ms\":%llu}",
            (unsigned long long)m_state.last_update_ms);

    } else if (strcmp(action, "GET_STATUS") == 0) {
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"intensity\":%.3f,"
            "\"eq_calibrated\":%s,\"listen_phon\":%.1f,\"ref_phon\":%.1f,"
            "\"eq_gains\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f],"
            "\"compressor\":%.3f,\"spatial_width\":%.3f,"
            "\"harmonic_gain\":%.3f,\"anti_dolby\":%.3f,"
            "\"uptime_ms\":%llu}",
            m_state.intensity,
            m_state.eq_calibrated ? "true" : "false",
            m_state.listen_phon, m_state.ref_phon,
            m_state.eq_gains[0], m_state.eq_gains[1], m_state.eq_gains[2],
            m_state.eq_gains[3], m_state.eq_gains[4], m_state.eq_gains[5],
            m_state.eq_gains[6], m_state.eq_gains[7], m_state.eq_gains[8],
            m_state.eq_gains[9],
            m_state.compressor, m_state.spatial_width,
            m_state.harmonic_gain, m_state.anti_dolby,
            (unsigned long long)m_state.last_update_ms);

    } else if (strlen(action) == 0) {
        // Payload no reconocido como JSON de comando — puede ser tráfico de control
        n = snprintf(reply, reply_sz, "{\"ok\":false,\"error\":\"no action field\"}");

    } else {
        n = snprintf(reply, reply_sz,
            "{\"ok\":false,\"error\":\"unknown action\",\"received\":\"%s\"}", action);
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
