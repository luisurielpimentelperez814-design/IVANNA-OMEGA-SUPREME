/**
 * OmegaDaemonV8.cpp — IVANNA OMEGA SUPREME
 * Daemon de audio system-wide para módulo Magisk.
 *
 * Socket: @omega_daemon_socket (abstract namespace, requiere root)
 * Protocolo: JSON sobre LocalSocket (UTF-8, sin longitud prefijada)
 *
 * Acciones soportadas:
 *   SET_PERCEPTUAL_STATE  — compressor, exciterReduction, highCutHz, etc.
 *   SET_INTENSITY         — intensidad global del procesamiento
 *   SET_PF_PARAMS         — 13 parámetros del PF Engine en bulk
 *   SET_ADAPTIVE_STATE    — targetGain, compAmount, excRed
 *   SET_YAMNET_SCORES     — speech, music, classId, confidence
 *   SET_ROUTE_PROFILE     — bassBoostDb, dialogBoostDb, widenerMult
 *   SET_SAF_STATE         — deltaEnergy, metricNorm, memory, gain
 *   SET_EQ_BANDS          — 10 gains ISO 226 en dB (nuevo)
 *   PING                  — probe de conectividad
 *   GET_STATUS            — estado completo del daemon
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

/* ── Configuración ────────────────────────────────────────────────────────── */
#define SOCKET_NAME       "omega_daemon_socket"   /* abstract namespace */
#define MAX_CLIENTS       8
#define RECV_BUF_SIZE     4096
#define SEND_BUF_SIZE     512
#define EQ_BANDS          10

/* ── Estado DSP compartido (actualizado por el socket, leído por el engine) ─ */
typedef struct {
    /* EQ ISO 226: 10 bandas en dB (31/63/125/250/500/1k/2k/4k/8k/12.5k Hz) */
    float eq_gains[EQ_BANDS];
    float listen_phon;
    float ref_phon;
    bool  eq_calibrated;

    /* Perceptual state */
    float compressor;
    float exciter_reduction;
    float high_cut_hz;
    float spatial_width;
    float loudness_target;
    float harmonic_gain;
    float anti_dolby;

    /* Adaptive state */
    float target_gain;
    float comp_amount;
    float exc_red;

    /* PF Engine */
    float pf_params[13];

    /* Route profile */
    float bass_boost_db;
    float dialog_boost_db;
    float widener_mult;

    /* SAF */
    float saf_delta_energy;
    float saf_metric_norm;
    float saf_memory;
    float saf_gain;

    /* General */
    float intensity;
    uint64_t last_update_ms;
} OmegaDspState;

static OmegaDspState g_state = {
    .eq_gains      = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    .listen_phon   = 60.f,
    .ref_phon      = 80.f,
    .eq_calibrated = false,
    .compressor    = 0.5f,
    .high_cut_hz   = 18000.f,
    .spatial_width = 1.0f,
    .loudness_target = -14.f,
    .harmonic_gain = 0.5f,
    .anti_dolby    = 1.0f,
    .intensity     = 0.85f,
    .widener_mult  = 1.0f,
};
static pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── Parser JSON minimalista (sin dependencias externas) ─────────────────── */
static float json_get_float(const char *json, const char *key, float def) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return def;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    char *end;
    float v = strtof(p, &end);
    return (end == p) ? def : v;
}

static bool json_get_array_floats(const char *json, const char *key,
                                   float *out, int max_n) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && *p != '[') p++;
    if (*p != '[') return false;
    p++;
    int n = 0;
    while (n < max_n && *p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
        char *end;
        float v = strtof(p, &end);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n > 0;
}

static const char* json_get_action(const char *json) {
    static char action[64];
    const char *p = strstr(json, "\"action\"");
    if (!p) { action[0] = '\0'; return action; }
    p += 8;
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && i < 63) action[i++] = *p++;
    action[i] = '\0';
    return action;
}

/* ── Dispatch de comandos ────────────────────────────────────────────────── */
static int handle_command(const char *json, char *reply, int reply_sz) {
    const char *action = json_get_action(json);
    pthread_mutex_lock(&g_state_mutex);
    g_state.last_update_ms = now_ms();
    int n = 0;

    if (strcmp(action, "SET_EQ_BANDS") == 0) {
        /* ISO 226:2003 — 10 gains en dB para el ecualizador system-wide */
        float gains[EQ_BANDS] = {0};
        bool ok = json_get_array_floats(json, "gains", gains, EQ_BANDS);
        if (ok) {
            for (int i = 0; i < EQ_BANDS; i++)
                g_state.eq_gains[i] = clampf(gains[i], -15.f, 15.f);
        }
        g_state.listen_phon   = json_get_float(json, "listenPhon", g_state.listen_phon);
        g_state.ref_phon      = json_get_float(json, "refPhon",    g_state.ref_phon);
        g_state.eq_calibrated = ok;
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"action\":\"SET_EQ_BANDS\","
            "\"calibrated\":%s,\"listenPhon\":%.1f,\"refPhon\":%.1f,"
            "\"gains\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]}",
            ok ? "true" : "false",
            g_state.listen_phon, g_state.ref_phon,
            g_state.eq_gains[0], g_state.eq_gains[1], g_state.eq_gains[2],
            g_state.eq_gains[3], g_state.eq_gains[4], g_state.eq_gains[5],
            g_state.eq_gains[6], g_state.eq_gains[7], g_state.eq_gains[8],
            g_state.eq_gains[9]);

    } else if (strcmp(action, "SET_PERCEPTUAL_STATE") == 0) {
        g_state.compressor       = json_get_float(json, "compressor",         g_state.compressor);
        g_state.exciter_reduction= json_get_float(json, "exciterReduction",   g_state.exciter_reduction);
        g_state.high_cut_hz      = json_get_float(json, "highCutHz",          g_state.high_cut_hz);
        g_state.spatial_width    = json_get_float(json, "spatialWidth",       g_state.spatial_width);
        g_state.loudness_target  = json_get_float(json, "loudnessTargetLuFS", g_state.loudness_target);
        g_state.harmonic_gain    = json_get_float(json, "harmonicGain",       g_state.harmonic_gain);
        g_state.anti_dolby       = json_get_float(json, "antiDolbyIntensity", g_state.anti_dolby);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_PERCEPTUAL_STATE\"}");

    } else if (strcmp(action, "SET_INTENSITY") == 0) {
        g_state.intensity = clampf(json_get_float(json, "intensity", g_state.intensity), 0.f, 1.f);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"intensity\":%.3f}", g_state.intensity);

    } else if (strcmp(action, "SET_PF_PARAMS") == 0) {
        json_get_array_floats(json, "params", g_state.pf_params, 13);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_PF_PARAMS\"}");

    } else if (strcmp(action, "SET_ADAPTIVE_STATE") == 0) {
        g_state.target_gain = json_get_float(json, "targetGain", g_state.target_gain);
        g_state.comp_amount = json_get_float(json, "compAmount", g_state.comp_amount);
        g_state.exc_red     = json_get_float(json, "excRed",     g_state.exc_red);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_ADAPTIVE_STATE\"}");

    } else if (strcmp(action, "SET_YAMNET_SCORES") == 0) {
        /* Solo ACK — los scores se usan para clasificación interna */
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_YAMNET_SCORES\"}");

    } else if (strcmp(action, "SET_ROUTE_PROFILE") == 0) {
        g_state.bass_boost_db  = json_get_float(json, "bassBoostDb",   g_state.bass_boost_db);
        g_state.dialog_boost_db= json_get_float(json, "dialogBoostDb", g_state.dialog_boost_db);
        g_state.widener_mult   = json_get_float(json, "widenerMult",   g_state.widener_mult);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_ROUTE_PROFILE\"}");

    } else if (strcmp(action, "SET_SAF_STATE") == 0) {
        g_state.saf_delta_energy = json_get_float(json, "deltaEnergy", g_state.saf_delta_energy);
        g_state.saf_metric_norm  = json_get_float(json, "metricNorm",  g_state.saf_metric_norm);
        g_state.saf_memory       = json_get_float(json, "memory",      g_state.saf_memory);
        g_state.saf_gain         = json_get_float(json, "gain",        g_state.saf_gain);
        n = snprintf(reply, reply_sz, "{\"ok\":true,\"action\":\"SET_SAF_STATE\"}");

    } else if (strcmp(action, "PING") == 0) {
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"pong\":true,\"uptime_ms\":%llu}",
            (unsigned long long)g_state.last_update_ms);

    } else if (strcmp(action, "GET_STATUS") == 0) {
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"intensity\":%.3f,"
            "\"eq_calibrated\":%s,\"listen_phon\":%.1f,\"ref_phon\":%.1f,"
            "\"eq_gains\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f],"
            "\"compressor\":%.3f,\"spatial_width\":%.3f,"
            "\"harmonic_gain\":%.3f,\"anti_dolby\":%.3f,"
            "\"uptime_ms\":%llu}",
            g_state.intensity,
            g_state.eq_calibrated ? "true" : "false",
            g_state.listen_phon, g_state.ref_phon,
            g_state.eq_gains[0], g_state.eq_gains[1], g_state.eq_gains[2],
            g_state.eq_gains[3], g_state.eq_gains[4], g_state.eq_gains[5],
            g_state.eq_gains[6], g_state.eq_gains[7], g_state.eq_gains[8],
            g_state.eq_gains[9],
            g_state.compressor, g_state.spatial_width,
            g_state.harmonic_gain, g_state.anti_dolby,
            (unsigned long long)g_state.last_update_ms);
    } else {
        n = snprintf(reply, reply_sz,
            "{\"ok\":false,\"error\":\"unknown action\",\"received\":\"%s\"}", action);
    }

    pthread_mutex_unlock(&g_state_mutex);
    return n;
}

/* ── Server loop ──────────────────────────────────────────────────────────── */
static void* client_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char recv_buf[RECV_BUF_SIZE];
    char send_buf[SEND_BUF_SIZE];

    ssize_t n = recv(fd, recv_buf, sizeof(recv_buf) - 1, 0);
    if (n > 0) {
        recv_buf[n] = '\0';
        int sn = handle_command(recv_buf, send_buf, sizeof(send_buf));
        if (sn > 0) send(fd, send_buf, (size_t)sn, 0);
    }
    close(fd);
    return NULL;
}

int main(void) {
    /* Ignorar SIGPIPE (cliente desconectado) */
    signal(SIGPIPE, SIG_IGN);

    /* Crear socket en abstract namespace (no requiere /tmp ni /dev/socket) */
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* Abstract namespace: sun_path[0] = '\0', resto = nombre */
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, SOCKET_NAME, sizeof(addr.sun_path) - 2);
    socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) +
                        1 + strlen(SOCKET_NAME);

    if (bind(srv, (struct sockaddr*)&addr, addrlen) < 0) {
        perror("bind @" SOCKET_NAME);
        close(srv);
        return 1;
    }

    listen(srv, MAX_CLIENTS);
    /* Señalar al sistema que el daemon está listo */
    fprintf(stdout, "OmegaDaemonV8: escuchando en @%s\n", SOCKET_NAME);
    fflush(stdout);

    for (;;) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) continue;
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, (void*)(intptr_t)client);
        pthread_attr_destroy(&attr);
    }

    close(srv);
    return 0;
}
