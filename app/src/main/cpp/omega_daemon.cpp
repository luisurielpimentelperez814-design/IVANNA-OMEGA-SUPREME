#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sched.h>
#include "IvannaFusionCore.hpp"

#define SOCKET_PATH "/dev/socket/ivanna_omega"
#define LOG_FILE "/data/adb/ivanna_omega/daemon.log"

static bool g_running = true;

void signalHandler(int signum) {
    g_running = false;
}

void logMessage(const std::string& msg) {
    std::ofstream log(LOG_FILE, std::ios::app);
    if (log.is_open()) {
        log << "[DAEMON] " << msg << std::endl;
    }
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    logMessage("IVANNA OMEGA SUPREME v6.0 Daemon starting...");

    // Set Real-Time Thread Priority
    struct sched_param param;
    param.sched_priority = 80;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        logMessage("Warning: Could not set SCHED_FIFO real-time priority.");
    }

    unlink(SOCKET_PATH);

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        logMessage("Error creating socket.");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logMessage("Error binding socket.");
        close(serverFd);
        return 1;
    }

    chmod(SOCKET_PATH, 0777);
    listen(serverFd, 5);

    logMessage("Daemon socket bound to " SOCKET_PATH ". Listening for client IPC...");

    while (g_running) {
        int clientFd = accept(serverFd, NULL, NULL);
        if (clientFd >= 0) {
            logMessage("Client connected.");
            char buffer[1024] = {0};
            ssize_t bytesRead = read(clientFd, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                logMessage(std::string("Received IPC: ") + buffer);
            }
            close(clientFd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    close(serverFd);
    unlink(SOCKET_PATH);
    logMessage("Daemon terminated safely.");
    return 0;
}
