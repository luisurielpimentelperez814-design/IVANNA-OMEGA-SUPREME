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

#define SOCKET_NAME   "omega_command_socket"
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
            "  PING | STATUS | GET_TELEMETRY\n"
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

    /* ── Conectar al abstract namespace ───────────────────────────────── */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* Abstract namespace: sun_path[0]='\0', nombre sin null inicial */
    size_t namelen = strlen(SOCKET_NAME);
    addr.sun_path[0] = '\0';
    memcpy(addr.sun_path + 1, SOCKET_NAME, namelen);
    socklen_t addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + namelen);

    if (connect(fd, (struct sockaddr*)&addr, addrlen) < 0) {
        fprintf(stderr, "ivanna_client: connect @%s: %s\n",
                SOCKET_NAME, strerror(errno));
        close(fd);
        return 1;
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
