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

#define LOG_TAG "IVANNA_OMEGA_DAEMON"

// Explicit path requirement: Must use /data/adb/ivanna_omega/omega_shm
constexpr const char* OMEGA_SHM_PATH = "/data/adb/ivanna_omega/omega_shm";
constexpr const char* OMEGA_DIR_PATH = "/data/adb/ivanna_omega";
constexpr const char* DEFAULT_LOG_PATH = "/data/adb/ivanna_daemon.log";
constexpr const char* DEFAULT_SOCKET_PATH = "/dev/socket/ivanna_omega";

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
            close(g_server_fd);
            g_server_fd = -1;
        }
        unlink(g_socket_path.c_str());
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
    ensure_directory_exists(OMEGA_DIR_PATH);

    log_message("Initializing Shared Memory at: " + std::string(OMEGA_SHM_PATH));

    // Open or create shared memory backing file directly at /data/adb/ivanna_omega/omega_shm
    int fd = open(OMEGA_SHM_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) {
        log_message("Error: Failed to open omega_shm file (" + std::string(OMEGA_SHM_PATH) + "): " + strerror(errno));
        return -1;
    }

    size_t shm_size = 65536; // 64KB real-time ring buffer
    if (ftruncate(fd, shm_size) != 0) {
        log_message("Error: ftruncate failed on omega_shm: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }

    void* ptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        log_message("Error: mmap failed for omega_shm: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }

    log_message("Shared Memory successfully mapped at " + std::string(OMEGA_SHM_PATH) + " (Size: " + std::to_string(shm_size) + " bytes)");
    close(fd);
    return 0;
}

int create_socket_server(const std::string& socket_path) {
    // Ensure parent socket folder exists
    std::string dir = socket_path;
    size_t last_slash = dir.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent_dir = dir.substr(0, last_slash);
        ensure_directory_exists(parent_dir.c_str());
    }

    // Unlink any existing socket file
    unlink(socket_path.c_str());

    int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        log_message("Error: Socket creation failed: " + std::string(strerror(errno)));
        return -1;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_message("Error: Socket bind failed at " + socket_path + ": " + std::string(strerror(errno)));
        close(server_fd);
        return -1;
    }

    chmod(socket_path.c_str(), 0666);

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

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            rate = std::stoi(argv[++i]);
        } else if (arg == "--buffer" && i + 1 < argc) {
            buffer = std::stoi(argv[++i]);
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
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

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
    if (setup_shared_memory() != 0) {
        log_message("Warning: Shared memory setup encountered errors. Continuing with socket service.");
    }

    // Create UNIX socket server
    g_server_fd = create_socket_server(socket_path);
    if (g_server_fd < 0) {
        log_message("Fatal Error: Could not initialize socket server. Exiting.");
        return 1;
    }

    log_message("IVANNA OMEGA Daemon running successfully.");

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
                log_message("Client connection accepted on " + socket_path);
                const char* ack_msg = "IVANNA_OMEGA_OK\n";
                write(client_fd, ack_msg, std::strlen(ack_msg));
                close(client_fd);
            }
        }
    }

    log_message("IVANNA OMEGA Daemon shutdown complete.");
    return 0;
}
