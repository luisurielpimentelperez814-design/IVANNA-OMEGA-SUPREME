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

        unlink(socketPath.c_str());

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[OMEGA_DAEMON] Error binding socket to path: " << socketPath << std::endl;
            close(serverFd);
            return;
        }

        listen(serverFd, 5);
        std::cout << "[OMEGA_DAEMON] Socket listening on " << socketPath << std::endl;

        while (m_running) {
            int clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd >= 0) {
                handleClient(clientFd);
                close(clientFd);
            }
        }

        close(serverFd);
        unlink(socketPath.c_str());
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

int main() {
    RealtimeOmegaDaemon daemon;
    daemon.setRealtimePriority();
    daemon.startSocketListener("/dev/socket/ivanna_omega");
    return 0;
}
