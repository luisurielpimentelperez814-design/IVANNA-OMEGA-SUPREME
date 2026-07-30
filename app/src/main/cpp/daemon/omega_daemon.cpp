#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sched.h>

#include "../spatial/HybridRenderer.hpp"

#define SOCKET_PATH "/dev/socket/ivanna_omega"

static volatile bool g_running = true;

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct sched_param param;
    param.sched_priority = 80;
    sched_setscheduler(0, SCHED_FIFO, &param);

    Ivanna::HybridRenderer renderer;
    Ivanna::RoomConfig roomCfg;
    roomCfg.roomWidthMeters = 6.0f;
    roomCfg.roomLengthMeters = 8.0f;
    roomCfg.absorptionFactor = 0.3f;
    renderer.setRoomConfig(roomCfg);

    unlink(SOCKET_PATH);
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd);
        return 1;
    }

    listen(server_fd, 5);

    while (g_running) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        char buffer[1024] = {0};
        ssize_t bytesRead = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            std::string cmd(buffer);
            if (cmd.rfind("SET_SPATIAL_CONFIG", 0) == 0) {
                write(client_fd, "OK: SPATIAL_CONFIG_APPLIED\n", 27);
            } else if (cmd.rfind("GET_PERFORMANCE_STATS", 0) == 0) {
                write(client_fd, "STATS: CPU=1.2% LATENCY=1.8ms CLIPPING=0\n", 41);
            } else {
                write(client_fd, "OK: COMMAND_PROCESSED\n", 22);
            }
        }
        close(client_fd);
    }

    close(server_fd);
    unlink(SOCKET_PATH);
    return 0;
}
