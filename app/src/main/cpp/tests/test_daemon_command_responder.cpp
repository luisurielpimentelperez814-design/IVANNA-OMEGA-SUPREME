// test_daemon_command_responder.cpp — Regresión v2.5.440 (Ruta B muerta).
//
// CAUSA RAÍZ: el daemon v2 bindeaba el socket abstracto @omega_daemon_socket
// pero nadie aceptaba conexiones (el bucle drenaba accept()+close()): la
// app preguntaba GET_STATUS y jamás recibía respuesta, así que el efecto
// omega nunca se acoplaba a la sesión. Este test fija el contrato que
// repone control/daemon_command_responder.h:
//   1. HELLO con proto 2 -> proto 3, compatible true.
//   2. HELLO con proto 99 -> compatible false.
//   3. GET_STATUS con sesión viva 329/gen 8 -> JSON con ok:true y los
//      campos que parseControlPlaneEvidence (Kotlin) exige, incluida la
//      sesión descubierta.
//   4. Acción desconocida -> {"ok":false,"error":"unsupported"}.
//   5. Petición no-JSON -> sin respuesta (conexión cerrada, fail-closed).
//   6. Conexión silenciosa (sin datos en 150 ms) -> cerrada sin respuesta.
//   7. Mapeo beacon -> vista de estado (Processing/EnabledIdle/NoEffect/
//      Unavailable) puro, sin I/O.
//   8. Round-trip real por socket abstracto: el hilo servidor responde a
//      un cliente que escribe el JSON y lee hasta '}' balanceado (igual que
//      OmegaEngineBridge.exchange()).
//
// Sin NDK, sin red: sockets UNIX abstractos de Linux + std::thread.
// El socket de prueba usa nombre único por PID para no chocar bajo ctest -j.
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "../daemon/control/daemon_command_responder.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const char* name) {
    if (ok) {
        ++g_pass;
        std::printf("  PASS: %s\n", name);
    } else {
        ++g_fail;
        std::printf("  FALLA: %s\n", name);
    }
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

// Cliente mínimo que replica OmegaEngineBridge.exchange(): conecta, escribe
// el JSON, lee hasta '}' balanceado o EOF/timeout. Devuelve la respuesta
// (vacía si el servidor cerró sin responder).
std::string exchangeOnce(const std::string& sockName, const std::string& req,
                         int timeoutMs = 3000) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return std::string();
    struct timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const socklen_t len = static_cast<socklen_t>(
        offsetof(struct sockaddr_un, sun_path) + 1 + sockName.size());
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, sockName.data(), sockName.size());
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0) {
        ::close(fd);
        return std::string();
    }
    if (!req.empty()) {
        (void)::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
    }
    std::string out;
    char buf[1024];
    int depth = 0;
    bool started = false;
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            const char c = buf[i];
            out.push_back(c);
            if (c == '{') { ++depth; started = true; }
            if (c == '}' && started && --depth == 0) {
                ::close(fd);
                return out;
            }
        }
        if (out.size() > 65536) break;
    }
    ::close(fd);
    return out;
}

int makeAbstractListener(const std::string& sockName) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const socklen_t len = static_cast<socklen_t>(
        offsetof(struct sockaddr_un, sun_path) + 1 + sockName.size());
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, sockName.data(), sockName.size());
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

int main() {
    using namespace ivanna::daemon;

    std::printf("test_daemon_command_responder (v2.5.440)\n");

    // ── 1-4. Despacho puro (sin sockets) ──────────────────────────────────
    {
        CommandPlaneState st;
        st.sessionId.store(329, std::memory_order_relaxed);
        st.sessionGen.store(8, std::memory_order_relaxed);
        st.generation.store(48440, std::memory_order_relaxed);
        st.verdict.store(IVANNA_VERDICT_BYPASS, std::memory_order_relaxed);
        st.startMs.store(1000, std::memory_order_relaxed);

        const std::string hello = handleCommand("{\"action\":\"HELLO\",\"proto\":2}", st);
        check(contains(hello, "\"ok\":true"), "HELLO proto 2 -> ok:true");
        check(contains(hello, "\"proto\":3"), "HELLO proto 2 -> proto 3");
        check(contains(hello, "\"compatible\":true"), "HELLO proto 2 -> compatible true");

        const std::string helloNew = handleCommand("{\"action\":\"HELLO\",\"proto\":99}", st);
        check(contains(helloNew, "\"compatible\":false"), "HELLO proto 99 -> compatible false");

        const std::string status = buildStatusReply(st, 5000);
        check(contains(status, "\"ok\":true"), "GET_STATUS -> ok:true");
        check(contains(status, "\"discovered_session_id\":329"), "GET_STATUS -> session 329");
        check(contains(status, "\"discovered_session_gen\":8"), "GET_STATUS -> gen 8");
        check(contains(status, "\"generation\":48440"), "GET_STATUS -> generation 48440");
        check(contains(status, "\"effect_state\":-1"), "GET_STATUS sin beacon -> effect_state -1");
        check(contains(status, "\"status\":\"bypass\""), "GET_STATUS sin beacon -> status bypass");
        check(contains(status, "\"upmixing_enabled\":0.0"), "GET_STATUS -> upmixing 0.0");
        check(contains(status, "\"wfs_enabled\":0.0"), "GET_STATUS -> wfs 0.0");
        check(contains(status, "\"uptime_ms\":4000"), "GET_STATUS -> uptime 4000 ms");
        check(contains(status, "\"consumer\":null"), "GET_STATUS sin efecto -> consumer null");

        const std::string unknown = handleCommand("{\"action\":\"SET_INTENSITY\",\"intensity\":0.5}", st);
        check(contains(unknown, "\"ok\":false") && contains(unknown, "unsupported"),
              "acción desconocida -> ok:false unsupported");

        const std::string bad = handleCommand("esto no es json", st);
        check(contains(bad, "\"ok\":false") && contains(bad, "bad_request"),
              "petición sin action -> ok:false bad_request");
    }

    // ── 7. Mapeo beacon -> vista de estado (puro) ──────────────────────────
    {
        StatusView v = computeStatusView(100, ivanna::EffectBeaconState::Processing,
                                         12345, 100, IVANNA_VERDICT_BYPASS);
        check(v.effectState == 2 && std::string(v.status) == "dsp_block_applied" &&
              std::string(v.consumer) == "omega_effect" && v.dspFrames == 12345,
              "beacon Processing + gen aplicada -> dsp_block_applied");
        v = computeStatusView(100, ivanna::EffectBeaconState::Processing,
                              12345, 90, IVANNA_VERDICT_BYPASS);
        check(std::string(v.status) == "awaiting_dsp_generation",
              "beacon Processing + gen vieja -> awaiting_dsp_generation");
        v = computeStatusView(100, ivanna::EffectBeaconState::EnabledIdle,
                              0, 0, IVANNA_VERDICT_BYPASS);
        check(v.effectState == 1 && std::string(v.status) == "effect_enabled_idle",
              "beacon EnabledIdle -> effect_enabled_idle");
        v = computeStatusView(100, ivanna::EffectBeaconState::NoEffect,
                              0, 0, IVANNA_VERDICT_LOADED);
        check(v.effectState == 0 && std::string(v.status) == "loaded",
              "beacon NoEffect -> status del veredicto (loaded)");
        v = computeStatusView(100, ivanna::EffectBeaconState::Unavailable,
                              0, 0, IVANNA_VERDICT_BYPASS);
        check(v.effectState == -1 && std::string(v.consumer) == "null",
              "beacon Unavailable -> effect_state -1, consumer null");
    }

    // ── 5, 6, 8. Round-trip real por socket abstracto ──────────────────────
    {
        const std::string sockName =
            "ivanna_test_cmd_" + std::to_string(static_cast<long long>(::getpid()));
        const int listenFd = makeAbstractListener(sockName);
        check(listenFd >= 0, "socket abstracto de prueba bindeado");
        if (listenFd >= 0) {
            CommandPlaneState st;
            st.sessionId.store(329, std::memory_order_relaxed);
            st.sessionGen.store(8, std::memory_order_relaxed);
            st.generation.store(48440, std::memory_order_relaxed);
            st.verdict.store(IVANNA_VERDICT_LOADED, std::memory_order_relaxed);
            st.startMs.store(1000, std::memory_order_relaxed);
            std::atomic<bool> running{true};
            // enforcePeerAuth=false: en host no existe /data/data/com.ivanna.omega
            // (el UID del test no estaría autorizado); en el daemon es true.
            std::thread server(serveCommandSocket, listenFd, std::ref(st),
                               std::cref(running), false);

            const std::string r1 = exchangeOnce(sockName, "{\"action\":\"GET_STATUS\"}");
            check(contains(r1, "\"ok\":true") &&
                  contains(r1, "\"discovered_session_id\":329") &&
                  contains(r1, "\"discovered_session_gen\":8") &&
                  contains(r1, "\"status\":\"loaded\""),
                  "socket: GET_STATUS responde sesión 329/gen 8 (veredicto loaded)");

            const std::string r2 = exchangeOnce(sockName, "{\"action\":\"HELLO\",\"proto\":2}");
            check(contains(r2, "\"proto\":3") && contains(r2, "\"compatible\":true"),
                  "socket: HELLO responde proto 3");

            const std::string r3 = exchangeOnce(sockName, "basura sin llaves");
            check(r3.empty(), "socket: petición no-JSON -> sin respuesta");

            // Conexión silenciosa: sin escribir nada, el servidor cierra
            // a los ~150 ms sin responder (recv devuelve 0/EOF).
            const std::string r4 = exchangeOnce(sockName, "", 3000);
            check(r4.empty(), "socket: conexión silenciosa -> cerrada sin respuesta");

            // Una segunda consulta tras las anteriores: el servidor sigue
            // vivo (un comando por conexión, sin estado stale).
            const std::string r5 = exchangeOnce(sockName, "{\"action\":\"GET_STATUS\"}");
            check(contains(r5, "\"discovered_session_id\":329"),
                  "socket: segunda GET_STATUS tras varios comandos -> sigue vivo");
            check(st.clientsServed.load() >= 5, "socket: clients_served cuenta las conexiones");

            running.store(false);
            server.join();
            ::close(listenFd);
        }
    }

    std::printf("RESULTADO: %d pasan, %d fallan\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
