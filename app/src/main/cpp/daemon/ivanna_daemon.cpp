#include "control/command_server.h"
#include "core/shm_manager.h"
/**
 * IVANNA-OMEGA-SUPREME Native Daemon
 * Architecture: ARM64 (arm64-v8a)
 * Android API: 35
 * Language: C++17
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstddef>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <android/log.h>
#include <thread>

#define LOG_TAG "IVANNA_OMEGA_DAEMON"

// Explicit path requirement: Must use /data/adb/ivanna_omega/omega_shm
constexpr const char* OMEGA_SHM_PATH = "/data/adb/ivanna_omega/omega_shm";
constexpr const char* OMEGA_DIR_PATH = "/data/adb/ivanna_omega";
// FIX: el log interno del daemon escribía en /data/adb/ivanna_daemon.log
// mientras service.sh (y cualquier diagnóstico documentado) lee
// /data/adb/ivanna_omega/daemon.log — el stdout/stderr del proceso ya se
// redirige ahí desde service.sh, pero los mensajes vía log_message() iban
// a un archivo que nadie mira. Unificado al path canónico del estado.
constexpr const char* DEFAULT_LOG_PATH = "/data/adb/ivanna_omega/daemon.log";
constexpr const char* DEFAULT_SOCKET_PATH = "@omega_daemon_socket";

// Global running status for clean signal shutdown
static volatile sig_atomic_t g_running = 1;
static int g_server_fd = -1;
static std::string g_socket_path = DEFAULT_SOCKET_PATH;
static std::string g_log_path = DEFAULT_LOG_PATH;

void log_message(const std::string& msg) {
    // 1. Android Logcat output via liblog.so
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());

    // 2. File log output to /data/adb/ivanna_daemon.log
    std::ofstream log_file(g_log_path, std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        log_file << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
                 << " [IVANNA-DAEMON] " << msg << std::endl;
        log_file.close();
    }
}

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        log_message("Signal " + std::to_string(signal) + " received. Stopping IVANNA OMEGA daemon...");
        g_running = 0;
        if (g_server_fd >= 0) {
            // FIX Foco #6: shutdown(SHUT_RDWR) antes de close() para que
            // select() retorne INMEDIATAMENTE con EBADF/EINTR en kernels 4.9
            // donde el fd cerrado no interrumpe un select() en vuelo.
            // Sin shutdown(), el loop principal puede quedar bloqueado en
            // select(g_server_fd+1,...) hasta que llegue una conexión o
            // venza el timeval de 1s, retrasando el apagado del daemon.
            shutdown(g_server_fd, SHUT_RDWR);
            close(g_server_fd);
            g_server_fd = -1;
        }
        if (!g_socket_path.empty() && g_socket_path[0] != '@') {
            unlink(g_socket_path.c_str());
        }
    }
}

void ensure_directory_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            log_message("Warning: Failed to create directory " + std::string(path) + ": " + strerror(errno));
        } else {
            log_message("Created directory: " + std::string(path));
        }
    }
}

int setup_shared_memory() {
    // FIX: usar OmegaShmManager (core/shm_manager.cpp) en vez de código
    // SHM inline. La lógica de mmap/mlock/seqlock queda encapsulada en
    // shmManager() y disponible para command_server.cpp y futuras extensiones
    // del daemon sin duplicar el boilerplate.
    ensure_directory_exists(OMEGA_DIR_PATH);
    log_message("Initializing Shared Memory via OmegaShmManager at: " +
                std::string(OMEGA_SHM_PATH));

    if (!ivanna::shmManager().init(OMEGA_SHM_PATH)) {
        log_message("Error: OmegaShmManager::init() falló en " +
                    std::string(OMEGA_SHM_PATH));
        return -1;
    }

    log_message("Shared Memory listo (OmegaShmManager): fd=" +
                std::to_string(ivanna::shmManager().fd()) +
                " base=" + std::to_string(
                    reinterpret_cast<uintptr_t>(ivanna::shmManager().base())) +
                " size=" + std::to_string(ivanna::shmManager().size()));
    return ivanna::shmManager().fd();
}

int create_socket_server(const std::string& socket_path) {
    // Ensure parent socket folder exists
    std::string dir = socket_path;
    size_t last_slash = (socket_path[0] == '@') ? std::string::npos
                                                : dir.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent_dir = dir.substr(0, last_slash);
        ensure_directory_exists(parent_dir.c_str());
    }

    // Unlink any existing socket file (solo filesystem: los abstract no existen
    // en el arbol de ficheros y unlink() aqui solo generaba ruido ENOENT).
    if (socket_path[0] != '@') {
        unlink(socket_path.c_str());
    }

    int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        log_message("Error: Socket creation failed: " + std::string(strerror(errno)));
        return -1;
    }

    // ── SO_REUSEADDR (Foco #5, auditoría 2026-08-09) ─────────────────────────
    // Sin este flag, si el daemon crashea rápido (SEGV en init de SHM, panic
    // por SELinux, kill -9 externo) antes de llegar al close(g_server_fd) del
    // signal handler, el kernel Linux mantiene el nombre @omega_daemon_socket
    // en TIME_WAIT interno del abstract namespace durante varios segundos.
    // El watchdog de service.sh reintenta el bind() ~0.5s después y recibe
    // EADDRINUSE → entra en backoff exponencial (2→4→8→…→60s) sin razón real,
    // dejando al usuario sin daemon durante minutos tras un solo crash.
    //
    // SO_REUSEADDR le dice al kernel: si el nombre está en cooldown pero no
    // hay ningún proceso vivo bindeando, entrégalo. NO permite double-bind
    // simultáneo (eso sería SO_REUSEPORT), sólo acelera la recuperación.
    //
    // Aplica igual a sockets de filesystem: si el daemon crashea sin unlink,
    // el archivo persiste y el próximo bind() falla; SO_REUSEADDR + el unlink
    // condicional del código ya existente cierran el ciclo.
    {
        int one = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                       &one, sizeof(one)) < 0) {
            // No es fatal: log y seguir. Sólo alarga el TTR tras crashes.
            log_message("Warning: setsockopt SO_REUSEADDR failed: " +
                        std::string(strerror(errno)));
        }
    }

    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (socket_path[0] == '@') {
        // Abstract socket: null byte prefix + name (no filesystem path).
        //
        // FIX CRITICO (causa raiz de "socket OFFLINE / queued" permanente):
        //   antes se pasaba sizeof(addr) como addrlen. En Linux, para el
        //   abstract namespace el NOMBRE es TODO el rango [sun_path, addrlen),
        //   asi que el kernel registraba "omega_daemon_socket" seguido de 88
        //   bytes NUL. Android LocalSocket (Namespace.ABSTRACT) conecta con
        //   addrlen = offsetof(sun_path) + 1 + strlen(name), es decir el
        //   nombre SIN padding: los dos nombres no coinciden y todo connect()
        //   desde la app moria con ECONNREFUSED. El daemon parecia sano en el
        //   log y la UI marcaba OFFLINE. Aqui se calcula el addrlen exacto.
        const std::string name = socket_path.substr(1);
        if (name.size() > sizeof(addr.sun_path) - 2) {
            log_message("Error: abstract socket name demasiado largo: " + name);
            close(server_fd);
            return -1;
        }
        addr.sun_path[0] = '\0';
        std::memcpy(addr.sun_path + 1, name.c_str(), name.size());
        addr_len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path)
                                          + 1 + name.size());
    } else {
        // Filesystem socket
        std::strncpy(addr.sun_path,
                     socket_path.c_str(),
                     sizeof(addr.sun_path) - 1);
        addr_len = static_cast<socklen_t>(sizeof(addr));
    }

    if (bind(server_fd, (struct sockaddr*)&addr, addr_len) < 0) {
        log_message("Error: Socket bind failed at " + socket_path + ": " + std::string(strerror(errno)));
        close(server_fd);
        return -1;
    }

    // chmod/unlink solo tienen sentido en sockets de filesystem: con un nombre
    // abstracto creaban/tocaban un fichero literal "@omega_daemon_socket" en el
    // CWD del daemon y devolvian ENOENT en cada arranque.
    if (socket_path[0] != '@') {
        chmod(socket_path.c_str(), 0666);
    }

    if (listen(server_fd, 16) < 0) {
        log_message("Error: Socket listen failed: " + std::string(strerror(errno)));
        close(server_fd);
        unlink(socket_path.c_str());
        return -1;
    }

    log_message("Socket server active and listening on: " + socket_path);
    return server_fd;
}

int main(int argc, char* argv[]) {
    std::string socket_path = DEFAULT_SOCKET_PATH;
    int rate = 48000;
    int buffer = 64;
    bool realtime = false;

    // El log vive dentro de /data/adb/ivanna_omega (ver DEFAULT_LOG_PATH).
    // service.sh ya lo crea con mkdir -p antes de lanzarnos, pero en un
    // arranque standalone (pruebas manuales desde shell) el directorio
    // puede no existir y los primeros log_message() se perderían en
    // silencio. Crearlo aquí es barato e idempotente.
    ensure_directory_exists(OMEGA_DIR_PATH);

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            // FIX: std::stoi sin try/catch lanzaba std::invalid_argument y
            // abortaba el daemon antes de publicar el socket — el watchdog
            // de service.sh lo reiniciaba en backoff exponencial y la UI
            // mostraba DETENIDO de forma permanente.
            try { rate = std::stoi(argv[++i]); }
            catch (...) { log_message("WARN: --rate invalido, usando 48000"); ++i; rate = 48000; }
        } else if (arg == "--buffer" && i + 1 < argc) {
            try { buffer = std::stoi(argv[++i]); }
            catch (...) { log_message("WARN: --buffer invalido, usando 64"); ++i; buffer = 64; }
        } else if (arg == "--realtime") {
            realtime = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "IVANNA-OMEGA-SUPREME Daemon v3.5.0\n"
                      << "Usage: ivanna_daemon [OPTIONS]\n"
                      << "Options:\n"
                      << "  --socket <path>   Unix socket path (default: " << DEFAULT_SOCKET_PATH << ")\n"
                      << "  --rate <hz>       Audio sample rate (default: 48000)\n"
                      << "  --buffer <size>   Audio buffer size (default: 64)\n"
                      << "  --realtime        Enable SCHED_FIFO realtime priority\n";
            return 0;
        }
    }

    g_socket_path = socket_path;

    // Setup signal handling
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    // FIX CRÍTICO: ignorar SIGPIPE.
    // send() / sendmsg() sin MSG_NOSIGNAL entregan SIGPIPE si el cliente
    // Kotlin cierra la conexión antes del send del daemon (race normal entre
    // el timeout de soTimeout en OmegaEngineBridge y el dispatch del daemon).
    // La acción por defecto de SIGPIPE es TERMINAR el proceso — el daemon
    // moría silenciosamente, el watchdog lo reiniciaba con backoff exponencial
    // y la UI mostraba OFFLINE por hasta 60 s. SIG_IGN hace que send()
    // devuelva -1/EPIPE en vez de matar el proceso.
    signal(SIGPIPE, SIG_IGN);

    log_message("=================================================");
    log_message("IVANNA-OMEGA-SUPREME Daemon Starting...");
    log_message("Architecture: ARM64-v8a | Android API 35");
    log_message("Shared Memory Target: " + std::string(OMEGA_SHM_PATH));
    log_message("Configuration -> Socket: " + socket_path + " | Rate: " + std::to_string(rate) +
                " Hz | Buffer: " + std::to_string(buffer) + " | Realtime: " + (realtime ? "ENABLED" : "DISABLED"));

    if (realtime) {
        struct sched_param param;
        param.sched_priority = 80;
        if (sched_setscheduler(0, SCHED_FIFO, &param) == 0) {
            log_message("Realtime SCHED_FIFO scheduling policy applied successfully (Priority 80).");
        } else {
            log_message("Notice: Could not set SCHED_FIFO priority (" + std::string(strerror(errno)) + "). Continuing in normal mode.");
        }
    }

    // Initialize shared memory
    // FIX: setup_shared_memory() devuelve el FD del SHM (positivo) en exito y
    // -1 en error; el chequeo "!= 0" daba SIEMPRE verdadero y escupia el
    // warning de fallo incluso con la memoria compartida perfectamente lista.
    if (setup_shared_memory() < 0) {
        log_message("Warning: Shared memory setup encountered errors. Continuing with socket service.");
    }

    // Create UNIX socket server
    g_server_fd = create_socket_server(socket_path);
    if (g_server_fd < 0) {
        log_message("Fatal Error: Could not initialize socket server. Exiting.");
        return 1;
    }

    

    
    std::string selinux_ctx = "UNKNOWN";
    FILE* fp = fopen("/proc/self/attr/current", "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            selinux_ctx = buf;
            if (!selinux_ctx.empty() && selinux_ctx.back() == '\n') selinux_ctx.pop_back();
        }
        fclose(fp);
    }
    log_message("Runtime Info -> PID: " + std::to_string(getpid()) + " | UID: " + std::to_string(getuid()) + " | SELinux: " + selinux_ctx);
    log_message("IVANNA OMEGA Daemon running successfully.");


// FIX (socket queued/offline intermitente — causa raíz encontrada):
//   g_server_fd (create_socket_server() arriba) YA está bind()eado en
//   @omega_daemon_socket y el select-loop de abajo YA despacha JSON/texto
//   sobre él a través de commandServer.handleJsonCommand/handleTextCommand.
//
//   El código anterior aquí intentaba ADEMÁS levantar un CommandServer
//   independiente con su propio bind() en el MISMO nombre abstracto
//   "@omega_daemon_socket". Un abstract socket solo admite un listener
//   por nombre: ese segundo bind() fallaba siempre con EADDRINUSE,
//   dejaba "ERROR starting @omega_daemon_socket" en el log en CADA
//   arranque del daemon, y era indistinguible en logcat de un fallo real
//   — exactamente el tipo de ruido que hace ver el socket como
//   queued/offline aunque el daemon esté sirviendo bien por el otro
//   camino.
//
//   commandServer se mantiene como el ÚNICO objeto de estado DSP
//   (m_state) que usa el select-loop de abajo — solo se quitó el bind()
//   duplicado y destinado a fallar. Su primera línea (m_state =
//   kDefaultState) sigue corriendo aquí mismo, así que el estado DSP
//   arranca con los defaults correctos.
CommandServer commandServer;
commandServer.resetState();

// @omega_command_socket sí es un nombre distinto — no compite con el
// anterior — y queda disponible para futuros clientes de control/admin
// que no sea OmegaEngineBridge (que hardcodea @omega_daemon_socket).
CommandServer controlServer;

if (controlServer.start("@omega_command_socket")) {
    log_message("CONTROL socket ready: @omega_command_socket");
    std::thread([&controlServer]() {
        controlServer.acceptLoop();
    }).detach();
} else {
    log_message("ERROR starting @omega_command_socket");
}

    // Daemon main loop
    while (g_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_server_fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(g_server_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) {
            log_message("Select socket error: " + std::string(strerror(errno)));
            break;
        }

        if (activity > 0 && FD_ISSET(g_server_fd, &readfds)) {
            struct sockaddr_un client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                
                struct ucred credentials;
                int ucred_length = sizeof(struct ucred);
                if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &credentials, (socklen_t*)&ucred_length) == 0) {
                    log_message("Client connection accepted on " + socket_path + " | UID: " + std::to_string(credentials.uid) + " | PID: " + std::to_string(credentials.pid));
                } else {
                    log_message("Client connection accepted on " + socket_path);
                }


                // FIX: demux socket — el mismo @omega_daemon_socket sirve dos propósitos:
                //
                //   Modo A — JSON command (OmegaEngineBridge Kotlin):
                //     El cliente envía {"action":"...",...} de inmediato.
                //     Leemos con timeout 5ms. Si llega JSON → commandServer.handleJsonCommand()
                //     → respuesta JSON. No se entrega ningún FD.
                //
                //   Modo B — SHM fd delivery (ShmManager.kt / cliente legacy):
                //     El cliente conecta sin enviar datos. Timeout expira → entregamos
                //     el fd del SHM vía SCM_RIGHTS como siempre.
                //
                // Por qué este socket y no solo @omega_command_socket:
                //   OmegaEngineBridge.kt hardcodea SOCKET_PRIMARY = "omega_daemon_socket".
                //   Redirigir el JSON aquí evita tocar el Kotlin y mantiene compatibilidad.

                // FIX (comandos perdidos / clasificados como modo B por error):
                //   el codigo anterior hacia UN solo recv() con SO_RCVTIMEO de
                //   5 ms. Dos fallos reales:
                //     1. 5 ms no alcanzan cuando la app esta bajo GC o el
                //        scheduler no la corre de inmediato: el timeout expiraba,
                //        el daemon creia "cliente SHM" (modo B) y mandaba un FD
                //        por SCM_RIGHTS a un cliente que en realidad esperaba
                //        una respuesta de texto -> "queued"/basura en la UI.
                //     2. un unico recv() no garantiza el mensaje completo:
                //        "SET_PF_DRIVE:0.5\n" podia llegar partido y el parser
                //        veia un comando truncado.
                //   Ahora: espera hasta 150 ms al PRIMER byte y luego drena
                //   hasta el delimitador ('\n' para texto, llaves balanceadas
                //   para JSON) o hasta agotar el buffer.
                struct timeval rcv_tv { .tv_sec = 0, .tv_usec = 150000 };  // 150ms
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                           &rcv_tv, sizeof(rcv_tv));
                struct timeval snd_tv { .tv_sec = 0, .tv_usec = 300000 };  // 300ms
                setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                           &snd_tv, sizeof(snd_tv));

                char json_buf[4096] = {};
                ssize_t nbytes = recv(client_fd, json_buf, sizeof(json_buf) - 1, 0);

                if (nbytes > 0) {
                    // Drenar el resto del mensaje con un timeout corto: el
                    // primer byte ya llego, el resto viene detras de inmediato.
                    struct timeval cont_tv { .tv_sec = 0, .tv_usec = 20000 };  // 20ms
                    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                               &cont_tv, sizeof(cont_tv));
                    const bool is_json = (json_buf[strspn(json_buf, " \t\r\n")] == '{');
                    while (nbytes < (ssize_t)sizeof(json_buf) - 1) {
                        json_buf[nbytes] = '\0';
                        if (!is_json && memchr(json_buf, '\n', (size_t)nbytes)) break;
                        if (is_json) {
                            int depth = 0; bool closed = false;
                            for (ssize_t i = 0; i < nbytes; ++i) {
                                if (json_buf[i] == '{') depth++;
                                else if (json_buf[i] == '}' && --depth == 0) { closed = true; break; }
                            }
                            if (closed) break;
                        }
                        ssize_t more = recv(client_fd, json_buf + nbytes,
                                            sizeof(json_buf) - 1 - (size_t)nbytes, 0);
                        if (more <= 0) break;
                        nbytes += more;
                    }
                }

                if (nbytes > 0) {
                    json_buf[nbytes] = '\0';
                    char reply[1024] = {};

                    // Detección de protocolo por primer carácter no-espacio:
                    //   '{' → JSON (OmegaEngineBridge)
                    //   otro → texto plano (MagiskBridge: "SET_PF_DRIVE:0.5\n")
                    //
                    // AUDIT FIX (demux race / socket contaminado): un cliente
                    // que ENVIÓ datos nunca es el cliente SHM (Modo B) — el
                    // cliente SHM legacy conecta en silencio. Antes, si el GC
                    // de la app retrasaba el payload más allá del timeout, el
                    // daemon clasificaba la conexión como Modo B y disparaba
                    // un FD binario por SCM_RIGHTS a un socket que esperaba
                    // texto -> "queued"/basura en la UI y congelamiento.
                    // Guardas añadidas:
                    //   1. Con datos recibidos, jamás se entra a Modo B.
                    //   2. El modo texto solo se despacha si el payload es
                    //      ASCII imprimible (descarta basura binaria en el
                    //      socket en vez de pasársela al parser).
                    //   3. Payload no-JSON y no-texto -> se descarta y se
                    //      cierra limpio. El protocolo legacy (timeout sin
                    //      bytes -> Modo B) queda intacto.
                    const char* p = json_buf;
                    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                    bool printable = true;
                    for (ssize_t i = 0; i < nbytes; ++i) {
                        const unsigned char c = (unsigned char)json_buf[i];
                        if (c < 0x09 || (c > 0x0D && c < 0x20) || c == 0x7F) {
                            printable = false;
                            break;
                        }
                    }
                    if (*p == '{') {
                        // ── Modo A: JSON command (OmegaEngineBridge) ─────────
                        int rlen = commandServer.handleJsonCommand(json_buf, reply, sizeof(reply));
                        log_message("JSON cmd dispatch: " + std::string(json_buf, std::min((ssize_t)60, nbytes)));
                        if (rlen > 0) {
                            send(client_fd, reply, (size_t)rlen, MSG_NOSIGNAL);
                        }
                    } else if (printable && nbytes > 1) {
                        // ── Modo A2: texto plano (MagiskBridge) ─────────────
                        int rlen = commandServer.handleTextCommand(json_buf, reply, sizeof(reply));
                        log_message("TEXT cmd dispatch: " + std::string(json_buf, std::min((ssize_t)60, nbytes)));
                        if (rlen > 0) {
                            // MSG_NOSIGNAL: si el cliente ya cerró, devuelve EPIPE
                            // en vez de entregar SIGPIPE al proceso.
                            send(client_fd, reply, (size_t)rlen, MSG_NOSIGNAL);
                        }
                    } else {
                        // ── Basura en el socket: ni JSON ni texto válido ──────
                        log_message("Demux: payload no-JSON/no-texto descartado (" +
                                    std::to_string(nbytes) + " bytes) — nunca se envía FD a un cliente que habló");
                    }

                } else {
                    // ── Modo B: SCM_RIGHTS SHM fd delivery ──────────────────
                    // FIX: usar el fd del shmManager en vez de re-abrir
                    int shm_fd = ivanna::shmManager().isReady()
                        ? dup(ivanna::shmManager().fd())
                        : open(OMEGA_SHM_PATH, O_RDWR | O_CLOEXEC);
                    if (shm_fd < 0) {
                        log_message("ERROR: shm_fd no disponible");
                        close(client_fd);
                        continue;
                    }

                    char data = 0;
                    struct iovec iov;
                    iov.iov_base = &data;
                    iov.iov_len = 1;

                    char control[CMSG_SPACE(sizeof(int))];
                    memset(control, 0, sizeof(control));

                    struct msghdr msg;
                    memset(&msg, 0, sizeof(msg));
                    msg.msg_iov = &iov;
                    msg.msg_iovlen = 1;
                    msg.msg_control = control;
                    msg.msg_controllen = sizeof(control);

                    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                    cmsg->cmsg_level = SOL_SOCKET;
                    cmsg->cmsg_type = SCM_RIGHTS;
                    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

                    memcpy(CMSG_DATA(cmsg), &shm_fd, sizeof(int));

                    if (sendmsg(client_fd, &msg, MSG_NOSIGNAL) < 0) {
                        log_message("ERROR sending SCM_RIGHTS");
                    } else {
                        log_message("omega_shm FD sent via SCM_RIGHTS");
                    }

                    close(shm_fd);
                }

                close(client_fd);
            }
        }
    }

    // Graceful shutdown cleanup
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }

    // FIX Foco #7: stop() llama shutdown()+close() en serverFd del control socket.
    // Sin esto, el std::thread detach que corre controlServer.acceptLoop() queda
    // bloqueado en accept4(@omega_command_socket) indefinidamente después de que
    // main() retorna — el proceso no puede hacer exit limpio y el watchdog de
    // service.sh ve un zombie hasta el siguiente ciclo de recolección del kernel.
    controlServer.stop();

    if (socket_path[0] != '@') {
        unlink(socket_path.c_str());
    }

    log_message("Socket resources released.");
    log_message("IVANNA OMEGA Daemon shutdown complete.");
    return 0;
}