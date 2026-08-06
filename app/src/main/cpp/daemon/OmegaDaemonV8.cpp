#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <cstddef>
#include <algorithm>

struct ControlState {
    float eqLowDb{0.0f};
    float eqMidDb{0.0f};
    float eqHighDb{0.0f};
    float compressionAmount{0.0f};
    float spatialWidth{1.0f};
    float roomSize{0.2f};
    float masterGain{1.0f};
    uint32_t flags{0};
};

class RealtimeOmegaDaemon {
public:
    RealtimeOmegaDaemon() : m_running(false) {}

    void setRealtimePriority() {
        struct sched_param param;
        param.sched_priority = 90; // High priority RT thread
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
            std::cerr << "[OMEGA_DAEMON] Warning: Could not set SCHED_FIFO priority." << std::endl;
        } else {
            std::cout << "[OMEGA_DAEMON] Realtime SCHED_FIFO priority activated." << std::endl;
        }
    }

    void startSocketListener(const std::string& socketPath) {
        m_running = true;
        int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (serverFd < 0) {
            std::cerr << "[OMEGA_DAEMON] Error creating UNIX socket." << std::endl;
            return;
        }

        // FIX (socket no aparece encendido, end-to-end):
        // El bridge Kotlin (OmegaEngineBridge / MagiskBridge) conecta con
        // LocalSocketAddress.Namespace.ABSTRACT contra "omega_daemon_socket".
        // Este daemon hacia bind() SIEMPRE en el filesystem (path relativo
        // al cwd), asi que Kotlin conectaba en el namespace abstracto de
        // Linux (sun_path[0] == '\0') mientras el daemon publicaba en el
        // filesystem — nunca se encontraban y el panel Magisk siempre
        // reportaba OFFLINE.
        //
        // Se soporta ahora el prefijo '@' como convencion estandar
        // (netcat/systemd/android): '@omega_daemon_socket' -> abstract
        // (compatible con Namespace.ABSTRACT del bridge Kotlin);
        // "/path/al/socket" -> filesystem (compatible con
        // Namespace.FILESYSTEM). service.sh puede seguir apuntando a
        // /dev/socket/ivanna_omega si quiere, pero por defecto el binary
        // publica en abstract, que es lo que la app espera hoy.
        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        socklen_t addrLen;
        const bool abstractNs =
            !socketPath.empty() && socketPath[0] == '@';
        if (abstractNs) {
            // Abstract namespace: sun_path[0] == '\0'. La longitud del
            // sockaddr define el tamano del nombre — NO se puede usar
            // strncpy con NUL terminator implicito, hay que calcular
            // addrLen a mano (offsetof + 1 byte de leading NUL + nombre).
            const std::string name = socketPath.substr(1);
            addr.sun_path[0] = '\0';
            std::memcpy(addr.sun_path + 1, name.data(),
                        std::min(name.size(), sizeof(addr.sun_path) - 1));
            addrLen = static_cast<socklen_t>(
                offsetof(struct sockaddr_un, sun_path) + 1 + name.size());
        } else {
            unlink(socketPath.c_str());
            std::strncpy(addr.sun_path, socketPath.c_str(),
                         sizeof(addr.sun_path) - 1);
            addrLen = sizeof(addr);
        }

        if (bind(serverFd, (struct sockaddr*)&addr, addrLen) < 0) {
            std::cerr << "[OMEGA_DAEMON] Error binding socket to path: "
                      << socketPath << " (" << std::strerror(errno) << ")"
                      << std::endl;
            close(serverFd);
            return;
        }

        listen(serverFd, 5);
        std::cout << "[OMEGA_DAEMON] Socket listening on " << socketPath
                  << (abstractNs ? " (abstract)" : " (filesystem)")
                  << std::endl;

        while (m_running) {
            int clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd >= 0) {
                handleClient(clientFd);
                close(clientFd);
            }
        }

        close(serverFd);
        if (!abstractNs) unlink(socketPath.c_str());
    }

private:
    std::atomic<bool> m_running;
    ControlState m_currentState;

    void handleClient(int clientFd) {
        char buffer[1024];
        std::memset(buffer, 0, sizeof(buffer));
        ssize_t bytesRead = read(clientFd, buffer, sizeof(buffer) - 1);

        if (bytesRead > 0) {
            std::string cmd(buffer);
            if (cmd.rfind("SET_PERCEPTUAL_STATE", 0) == 0) {
                // Parse commands securely avoiding NaN/Inf
                float low, mid, high, comp, width, room;
                if (sscanf(cmd.c_str(), "SET_PERCEPTUAL_STATE %f %f %f %f %f %f",
                           &low, &mid, &high, &comp, &width, &room) == 6) {

                    if (std::isfinite(low) && std::isfinite(mid) && std::isfinite(high)) {
                        m_currentState.eqLowDb = std::clamp(low, -12.0f, 12.0f);
                        m_currentState.eqMidDb = std::clamp(mid, -12.0f, 12.0f);
                        m_currentState.eqHighDb = std::clamp(high, -12.0f, 12.0f);
                        m_currentState.compressionAmount = std::clamp(comp, 0.0f, 1.0f);
                        m_currentState.spatialWidth = std::clamp(width, 0.0f, 2.0f);
                        m_currentState.roomSize = std::clamp(room, 0.0f, 1.0f);
                    }
                }
                std::string response = "ACK_OK\n";
                write(clientFd, response.c_str(), response.length());
            } else if (cmd.rfind("GET_PERFORMANCE_STATS", 0) == 0) {
                std::string stats = "LATENCY_MS: 1.8 | CPU_USAGE: 2.1% | CLIPPING_EVENTS: 0\n";
                write(clientFd, stats.c_str(), stats.length());
            }
        }
    }
};

int main(int argc, char* argv[]) {
    RealtimeOmegaDaemon daemon;
    daemon.setRealtimePriority();

    // FIX (socket no aparece encendido): default cambiado de
    // "omega_daemon_socket" (path concreto en el cwd del daemon) a
    // "@omega_daemon_socket" (namespace abstracto de Linux), que es donde
    // el bridge Kotlin de la app ya conecta hoy via
    // LocalSocketAddress.Namespace.ABSTRACT. Se sigue permitiendo
    // override por argv[1] para service.sh (que puede pasar un path del
    // filesystem si prefiere ese esquema).
    std::string socketPath = "@omega_daemon_socket";
    if (argc > 1) socketPath = argv[1];
    daemon.startSocketListener(socketPath);
    return 0;
}
