#include "control/command_server.h"
#include "core/shm_manager.h"
#include "../include/omega_shared.h"
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

constexpr const char* OMEGA_SHM_PATH = "/data/adb/ivanna_omega/omega_shm";
constexpr const char* OMEGA_DIR_PATH = "/data/adb/ivanna_omega";
constexpr const char* DEFAULT_LOG_PATH = "/data/adb/ivanna_omega/daemon.log";
constexpr const char* DEFAULT_SOCKET_PATH = "@omega_daemon_socket";

static volatile sig_atomic_t g_running = 1;
static int g_server_fd = -1;
static std::string g_socket_path = DEFAULT_SOCKET_PATH;
static std::string g_log_path = DEFAULT_LOG_PATH;

void log_message(const std::string& msg) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
    std::ofstream log_file(g_log_path, std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        log_file << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
                 << " [IVANNA-DAEMON] " << msg << std::endl;
    }
}

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        log_message("Signal " + std::to_string(signal) + " received. Stopping...");
        g_running = 0;
        if (g_server_fd >= 0) {
            shutdown(g_server_fd, SHUT_RDWR);
            close(g_server_fd);
            g_server_fd = -1;
        }
        if (!g_socket_path.empty() && g_socket_path[0] != '@') unlink(g_socket_path.c_str());
    }
}

void ensure_directory_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            log_message("Warning: Failed to create directory " + std::string(path) + ": " + strerror(errno));
        }
    }
}

int setup_shared_memory(int sampleRate) {
    ensure_directory_exists(OMEGA_DIR_PATH);

    log_message("Initializing Shared Memory via OmegaShmManager at: " 
        + std::string(OMEGA_SHM_PATH));

    if (!ivanna::shmManager().init(OMEGA_SHM_PATH)) {
        log_message("Error: OmegaShmManager::init() fallo");
        return -1;
    }

    void* base = ivanna::shmManager().base();

    if (base != nullptr) {
        auto* state =
            reinterpret_cast<OmegaSharedState*>(
                static_cast<uint8_t*>(base)
                + sizeof(ivanna::ShmHeader)
            );

        new(state) OmegaSharedState();

        state->is_processing.store(true);
        state->current_latency_ms.store(0.0f);

        log_message("OmegaSharedState inicializado dentro de SHM");
    }

    log_message("Shared Memory listo fd=" 
        + std::to_string(ivanna::shmManager().fd()));

    return ivanna::shmManager().fd();
}

int create_socket_server(const std::string& socket_path) {
    if (socket_path[0] != '@') unlink(socket_path.c_str());
    int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) return -1;
    int one = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_un addr; memset(&addr,0,sizeof(addr));
    addr.sun_family = AF_UNIX;
    socklen_t addr_len = 0;
    if (socket_path[0] == '@') {
        std::string name = socket_path.substr(1);
        addr.sun_path[0] = '\0';
        memcpy(addr.sun_path+1, name.c_str(), name.size());
        addr_len = offsetof(struct sockaddr_un, sun_path) + 1 + name.size();
    } else {
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path)-1);
        addr_len = sizeof(addr);
    }
    if (bind(server_fd, (struct sockaddr*)&addr, addr_len) < 0) { close(server_fd); return -1; }
    if (socket_path[0] != '@') chmod(socket_path.c_str(), 0666);
    if (listen(server_fd, 16) < 0) { close(server_fd); return -1; }
    log_message("Socket server active: " + socket_path);
    return server_fd;
}

int main(int argc, char* argv[]) {
    std::string socket_path = DEFAULT_SOCKET_PATH;
    int rate = 48000, buffer = 64; bool realtime = false;
    ensure_directory_exists(OMEGA_DIR_PATH);
    for (int i=1;i<argc;i++) {
        std::string arg=argv[i];
        if (arg=="--socket" && i+1<argc) socket_path=argv[++i];
        else if (arg=="--rate" && i+1<argc) { try{rate=std::stoi(argv[++i]);}catch(...){rate=48000;} }
        else if (arg=="--buffer" && i+1<argc) { try{buffer=std::stoi(argv[++i]);}catch(...){buffer=64;} }
        else if (arg=="--realtime") realtime=true;
    }
    g_socket_path=socket_path;
    struct sigaction sa; memset(&sa,0,sizeof(sa)); sa.sa_handler=signal_handler;
    sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr);
    signal(SIGPIPE, SIG_IGN);
    log_message("IVANNA-OMEGA Daemon v1.8 REAL TELEMETRY - Starting");
    if (realtime) { struct sched_param p; p.sched_priority=80; sched_setscheduler(0,SCHED_FIFO,&p); }
    setup_shared_memory(rate);
    log_message("DSP sample rate configured: " + std::to_string(rate));
    g_server_fd = create_socket_server(socket_path);
    if (g_server_fd<0) return 1;

    CommandServer commandServer;
    commandServer.resetState();
    CommandServer controlServer;
    if (controlServer.start("@omega_command_socket")) {
        log_message("CONTROL socket ready: @omega_command_socket");
        std::thread([&controlServer](){ controlServer.acceptLoop(); }).detach();
    }

    // FIX (daemon bloqueante / fd leak / buffer overflow):
    // 1. El accept loop anterior atendía clientes en el hilo principal de forma
    //    secuencial bloqueante — mientras un cliente tenía la conexión abierta
    //    (OmegaDaemon.kt mantiene conexión persistente), ningún otro podía
    //    conectarse. Se despacha cada cliente en un thread propio (detached).
    // 2. json_buf[8192] stack con recv parcial causaba lecturas fragmentadas
    //    sin NUL correcto. Se aumenta a 65536 y se acumula en heap.
    // 3. reply declarado dos veces en el mismo scope (UB/shadow) — unificado.
    while (g_running) {
        fd_set readfds; FD_ZERO(&readfds); FD_SET(g_server_fd,&readfds);
        struct timeval tv{1,0};
        int activity = select(g_server_fd+1,&readfds,NULL,NULL,&tv);
        if (activity<0 && errno!=EINTR) break;
        if (activity>0 && FD_ISSET(g_server_fd,&readfds)) {
            struct sockaddr_un client_addr; socklen_t client_len=sizeof(client_addr);
            int client_fd = accept(g_server_fd,(struct sockaddr*)&client_addr,&client_len);
            if (client_fd<0) continue;

            // FIX (concurrencia): antes el bucle principal atendia a UN cliente
            // en un recv-loop bloqueante. El bridge Kotlin mantiene su socket
            // abierto de forma persistente, asi que mientras ese cliente
            // mandara trafico el daemon jamas volvia a accept() y cualquier
            // otra conexion (reinstanciacion del engine, hot-reload) se
            // pudria en el backlog. Ahora cada conexion va a su propio hilo
            // (los handlers de CommandServer ya estan protegidos por mutex).
            std::thread([client_fd, &commandServer]() {
                struct timeval rcv_tv{0,150000}; setsockopt(client_fd,SOL_SOCKET,SO_RCVTIMEO,&rcv_tv,sizeof(rcv_tv));
                struct timeval snd_tv{0,300000}; setsockopt(client_fd,SOL_SOCKET,SO_SNDTIMEO,&snd_tv,sizeof(snd_tv));

                // FIX (framing): un socket stream NO garantiza mensajes
                // completos por recv() — un JSON grande podia llegar partido
                // y parsearse roto (comando perdido en silencio). Se acumula
                // y se extraen objetos JSON completos por balance de llaves
                // (respetando comillas y escapes); el resto queda en buffer
                // para el siguiente recv.
                std::string pending;
                pending.reserve(16384);
                char chunk[8192];
                while (g_running) {
                    ssize_t nbytes = recv(client_fd, chunk, sizeof(chunk), 0);
                    if (nbytes<=0) break;
                    pending.append(chunk, (size_t)nbytes);

                    // Descarta whitespace inter-mensaje.
                    size_t start = pending.find_first_not_of(" \t\r\n");
                    if (start==std::string::npos) { pending.clear(); continue; }
                    pending.erase(0, start);

                    if (pending[0]!='{') {
                        // Comandos de texto plano (ruta MagiskBridge): se
                        // procesa la linea completa hasta '\n' si existe, o el
                        // buffer entero (comportamiento historico).
                        std::string text = pending;
                        size_t nl = pending.find('\n');
                        if (nl!=std::string::npos) { text = pending.substr(0, nl); pending.erase(0, nl+1); }
                        else pending.clear();
                        char reply[1024]={};
                        int rlen = commandServer.handleTextCommand(text.c_str(), reply, sizeof(reply));
                        if (rlen>0) send(client_fd, reply, rlen, MSG_NOSIGNAL);
                        continue;
                    }

                    // Extrae el primer objeto JSON completo del buffer.
                    int depth=0; bool inStr=false, esc=false; size_t end=std::string::npos;
                    for (size_t i=0;i<pending.size();++i) {
                        char c=pending[i];
                        if (inStr) { if (esc) esc=false; else if (c=='\\') esc=true; else if (c=='\"') inStr=false; continue; }
                        if (c=='\"') inStr=true;
                        else if (c=='{') ++depth;
                        else if (c=='}' && --depth==0) { end=i+1; break; }
                    }
                    if (end==std::string::npos) {
                        // JSON incompleto: espera al siguiente fragmento.
                        if (pending.size() > 65536) pending.clear(); // proteccion anti-flood
                        continue;
                    }
                    std::string js = pending.substr(0, end);
                    pending.erase(0, end);

                    std::string action="UNKNOWN";
                    auto pos=js.find("\"action\"");
                    if (pos!=std::string::npos) {
                        auto p1=js.find("\"",pos+8); auto p2=js.find("\"",p1+1);
                        if (p1!=std::string::npos && p2!=std::string::npos) action=js.substr(p1+1,p2-p1-1);
                    }
                    char reply[4096]={};
                    int rlen = commandServer.handleJsonCommand(js.c_str(), reply, sizeof(reply));
                    if (rlen>0) {
                        send(client_fd, reply, rlen, MSG_NOSIGNAL);
                    } else {
                        std::string err = "{\"status\":\"error\",\"reason\":\"not_implemented\",\"action\":\"" + action + "\"}";
                        send(client_fd, err.c_str(), err.size(), MSG_NOSIGNAL);
                    }
                }
                close(client_fd);
            }).detach();
        }

    }
    if (g_server_fd>=0) close(g_server_fd);
    controlServer.stop();
    log_message("Daemon shutdown REAL");
    return 0;
}
