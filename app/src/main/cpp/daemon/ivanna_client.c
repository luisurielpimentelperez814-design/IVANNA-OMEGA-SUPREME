/*
 * ivanna_client.c — Cliente CLI nativo para @omega_command_socket
 *
 * Reemplaza la dependencia de `nc -U` (no disponible en /system/bin/nc
 * de muchos Android) con un binario ARM64 propio que conecta directamente
 * al abstract namespace AF_UNIX sin depender de busybox.
 *
 * Compilar con NDK r26:
 *   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android34-clang \
 *     -std=c11 -O2 -static-libgcc -pie -fPIE \
 *     -o ivanna_client ivanna_client.c
 *
 * Uso:
 *   ivanna_client PING
 *   ivanna_client STATUS
 *   ivanna_client SET_PRESET:Spatial
 *   ivanna_client SET_BYPASS:0
 *   ivanna_client SET_REVERB:0.35
 *   ivanna_client GET_TELEMETRY
 *
 * Protocolo: JSON sobre unix stream. El daemon responde con \n al final.
 * Timeout: 2 s por defecto (IVANNA_TIMEOUT_S env var para override).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <stddef.h>
#include <time.h>

#define SOCKET_PRIMARY   "omega_daemon_socket"
#define SOCKET_SECONDARY "omega_command_socket"
#define DEFAULT_TMO_S 2

/* ── Construye el JSON de comando a partir del argumento CLI ─────────── */
static int build_json(const char* arg, char* out, int outlen) {
    /* PING */
    if (!strcmp(arg, "PING"))
        return snprintf(out, outlen, "{\"action\":\"PING\"}");

    /* STATUS */
    if (!strcmp(arg, "STATUS"))
        return snprintf(out, outlen, "{\"action\":\"GET_TELEMETRY\"}");

    /* GET_TELEMETRY */
    if (!strcmp(arg, "GET_TELEMETRY"))
        return snprintf(out, outlen, "{\"action\":\"GET_TELEMETRY\"}");

    /* GET_SUPREME_AXES */
    if (!strcmp(arg, "GET_SUPREME_AXES") || !strcmp(arg, "SUPREME_STATUS"))
        return snprintf(out, outlen, "{\"action\":\"GET_SUPREME_AXES\"}");

    /* SET_SUPREME_PRESET:<flat|supreme|gaming|cinema> */
    if (!strncmp(arg, "SET_SUPREME_PRESET:", 19)) {
        const char* p = arg + 19;
        if (!strcmp(p, "flat")) {
            return snprintf(out, outlen,
                "{\"action\":\"SET_SUPREME_AXES\",\"latticeEnabled\":0,\"microChirp\":0,"
                "\"blDrive\":0.0,\"lambda\":0.72,\"cvnnEnabled\":0,\"harmonicGain\":0.0,"
                "\"imdCancel\":0.0,\"snnHoaEnabled\":0,\"snnImmersivity\":0.0,"
                "\"snnThreshold\":0.45,\"pinnaEnabled\":0,\"pinnaWetMix\":0.0,"
                "\"conchaDepth\":0.0,\"helixCurl\":0.0,\"headWidth\":0.0,"
                "\"farrowMsoEnabled\":0,\"msoItdNs\":0.0,\"ebpfBypassActive\":0}");
        } else if (!strcmp(p, "gaming")) {
            return snprintf(out, outlen,
                "{\"action\":\"SET_SUPREME_AXES\",\"latticeEnabled\":1,\"microChirp\":0,"
                "\"blDrive\":0.95,\"lambda\":0.72,\"cvnnEnabled\":1,\"harmonicGain\":0.25,"
                "\"imdCancel\":0.75,\"snnHoaEnabled\":1,\"snnImmersivity\":0.82,"
                "\"snnThreshold\":0.38,\"pinnaEnabled\":1,\"pinnaWetMix\":0.72,"
                "\"conchaDepth\":0.18,\"helixCurl\":-0.06,\"headWidth\":0.12,"
                "\"farrowMsoEnabled\":1,\"msoItdNs\":3500.0,\"ebpfBypassActive\":1}");
        } else if (!strcmp(p, "cinema")) {
            return snprintf(out, outlen,
                "{\"action\":\"SET_SUPREME_AXES\",\"latticeEnabled\":1,\"microChirp\":1,"
                "\"blDrive\":1.10,\"lambda\":0.74,\"cvnnEnabled\":1,\"harmonicGain\":0.40,"
                "\"imdCancel\":0.80,\"snnHoaEnabled\":1,\"snnImmersivity\":0.92,"
                "\"snnThreshold\":0.35,\"pinnaEnabled\":1,\"pinnaWetMix\":0.68,"
                "\"conchaDepth\":0.20,\"helixCurl\":-0.08,\"headWidth\":0.14,"
                "\"farrowMsoEnabled\":1,\"msoItdNs\":1200.0,\"ebpfBypassActive\":1}");
        } else {
            /* supreme (por defecto) */
            return snprintf(out, outlen,
                "{\"action\":\"SET_SUPREME_AXES\",\"latticeEnabled\":1,\"microChirp\":1,"
                "\"blDrive\":1.25,\"lambda\":0.756,\"cvnnEnabled\":1,\"harmonicGain\":0.45,"
                "\"imdCancel\":0.85,\"snnHoaEnabled\":1,\"snnImmersivity\":0.75,"
                "\"snnThreshold\":0.42,\"pinnaEnabled\":1,\"pinnaWetMix\":0.65,"
                "\"conchaDepth\":0.18,\"helixCurl\":-0.06,\"headWidth\":0.12,"
                "\"farrowMsoEnabled\":1,\"msoItdNs\":0.0,\"ebpfBypassActive\":1}");
        }
    }

    /* SET_PRESET:<name> */
    if (!strncmp(arg, "SET_PRESET:", 11))
        return snprintf(out, outlen,
            "{\"action\":\"SET_PRESET\",\"preset\":\"%s\"}", arg + 11);

    /* SET_BYPASS:<0|1> */
    if (!strncmp(arg, "SET_BYPASS:", 11))
        return snprintf(out, outlen,
            "{\"action\":\"SET_BYPASS\",\"bypass\":%s}",
            (arg[11] == '0') ? "false" : "true");

    /* SET_REVERB:<value> */
    if (!strncmp(arg, "SET_REVERB:", 11))
        return snprintf(out, outlen,
            "{\"action\":\"SET_ROOM_RT60\",\"rt60\":%s,\"wet\":0.35}", arg + 11);

    /* SET_VOLUME:<value 0-1> */
    if (!strncmp(arg, "SET_VOLUME:", 11))
        return snprintf(out, outlen,
            "{\"action\":\"SET_VOLUME\",\"volume\":%s}", arg + 11);

    /* Comando JSON directo */
    if (arg[0] == '{') {
        int n = (int)strlen(arg);
        if (n >= outlen) return -1;
        memcpy(out, arg, n + 1);
        return n;
    }

    fprintf(stderr, "ivanna_client: comando desconocido '%s'\n", arg);
    return -1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Uso: ivanna_client <CMD>\n"
            "  PING | STATUS | GET_TELEMETRY | GET_SUPREME_AXES\n"
            "  SET_SUPREME_PRESET:<flat|supreme|gaming|cinema>\n"
            "  SET_PRESET:<name>   SET_BYPASS:<0|1>\n"
            "  SET_REVERB:<rt60>   SET_VOLUME:<0-1>\n"
            "  '{\"action\":\"...\"}' (JSON directo)\n");
        return 2;
    }

    /* ── Timeout ───────────────────────────────────────────────────────── */
    int tmo = DEFAULT_TMO_S;
    const char* tmo_env = getenv("IVANNA_TIMEOUT_S");
    if (tmo_env) tmo = atoi(tmo_env);

    /* ── Construir JSON ────────────────────────────────────────────────── */
    char json[4096];
    int jlen = build_json(argv[1], json, (int)sizeof(json) - 2);
    if (jlen <= 0) return 2;
    json[jlen]     = '\n';  /* delimitador de mensaje */
    json[jlen + 1] = '\0';

    /* ── Crear socket ──────────────────────────────────────────────────── */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("ivanna_client: socket");
        return 1;
    }

    /* ── Timeout de recv ───────────────────────────────────────────────── */
    struct timeval tv = { .tv_sec = tmo, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* ── Conectar al abstract namespace (primario -> secundario) ──────── */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t namelen = strlen(SOCKET_PRIMARY);
    addr.sun_path[0] = '\0';
    memcpy(addr.sun_path + 1, SOCKET_PRIMARY, namelen);
    socklen_t addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + namelen);

    if (connect(fd, (struct sockaddr*)&addr, addrlen) < 0) {
        /* Fallback al socket secundario @omega_command_socket */
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        namelen = strlen(SOCKET_SECONDARY);
        addr.sun_path[0] = '\0';
        memcpy(addr.sun_path + 1, SOCKET_SECONDARY, namelen);
        addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + namelen);
        if (connect(fd, (struct sockaddr*)&addr, addrlen) < 0) {
            fprintf(stderr, "ivanna_client: connect @%s / @%s: %s\n",
                    SOCKET_PRIMARY, SOCKET_SECONDARY, strerror(errno));
            close(fd);
            return 1;
        }
    }

    /* ── Enviar comando ────────────────────────────────────────────────── */
    ssize_t sent = write(fd, json, jlen + 1);
    if (sent != jlen + 1) {
        perror("ivanna_client: write");
        close(fd);
        return 1;
    }

    /* ── Leer respuesta (hasta '\n' o EOF) ────────────────────────────── */
    char resp[65536];
    int pos = 0;
    ssize_t n;
    while (pos < (int)sizeof(resp) - 1 &&
           (n = read(fd, resp + pos, sizeof(resp) - pos - 1)) > 0) {
        pos += (int)n;
        if (resp[pos - 1] == '\n') break;
    }
    resp[pos] = '\0';
    close(fd);

    if (pos == 0) {
        fprintf(stderr, "ivanna_client: sin respuesta del daemon\n");
        return 1;
    }

    /* Imprimir respuesta y salir con 0 si contiene "ok":true */
    fputs(resp, stdout);
    if (!strchr(resp, '\n')) fputc('\n', stdout);

    return (strstr(resp, "\"ok\":true") || strstr(resp, "\"alive\"")) ? 0 : 1;
}
