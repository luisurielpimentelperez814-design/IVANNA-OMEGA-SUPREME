// omega_daemon_bridge.cpp — Puente SHM real entre ivanna_daemon y el proceso app.
// La app no abre /data/adb directamente: recibe el fd del daemon mediante
// SCM_RIGHTS en @omega_daemon_socket, y valida el layout antes de leerlo.
#include <android/log.h>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "include/omega_shared.h"
#include "daemon/core/shm_manager.h"
#include "include/omega_control_bus.h"

#define LOG_TAG "OmegaDaemonBridge"

static std::atomic<OmegaSharedState*> g_shared{nullptr};
static void* g_map = nullptr;
static size_t g_map_size = 0;
static std::atomic_flag g_connecting = ATOMIC_FLAG_INIT;

// Connect to the abstract Unix socket and receive the daemon-owned SHM fd.
// The daemon's documented "Modo B" sends one byte + SCM_RIGHTS after the
// first silent recv times out (~150 ms). Never fall back to opening /data/adb.
static int receiveDaemonShmFd() noexcept {
    const int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "socket() fallo errno=%d (%s)", errno, strerror(errno));
        return -1;
    }

    timeval timeout{};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    constexpr char kSocketName[] = "omega_daemon_socket";
    static_assert(sizeof(kSocketName) < sizeof(addr.sun_path),
                  "abstract socket name demasiado largo");
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, kSocketName, sizeof(kSocketName) - 1);
    const socklen_t addr_len = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + 1 + sizeof(kSocketName) - 1);

    if (connect(sock, reinterpret_cast<const sockaddr*>(&addr), addr_len) != 0) {
        const int e = errno;
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "connect @%s fallo errno=%d (%s)",
                            kSocketName, e, strerror(e));
        close(sock);
        return -1;
    }

    int received_fd = -1;
    int recv_errno = 0;
    const auto receive_result = ivanna::ipc::receiveExactlyOneFd(sock, &received_fd, &recv_errno);
    close(sock);
    if (receive_result != ivanna::ipc::ReceiveFdResult::kOk) {
        const char* reason = "unknown";
        switch (receive_result) {
            case ivanna::ipc::ReceiveFdResult::kRecvError: reason = "recvmsg"; break;
            case ivanna::ipc::ReceiveFdResult::kBadPayloadLength: reason = "bad payload length"; break;
            case ivanna::ipc::ReceiveFdResult::kTruncated: reason = "MSG_CTRUNC/MSG_TRUNC"; break;
            case ivanna::ipc::ReceiveFdResult::kMissingRights: reason = "missing SCM_RIGHTS"; break;
            case ivanna::ipc::ReceiveFdResult::kMalformedControl: reason = "malformed control message"; break;
            case ivanna::ipc::ReceiveFdResult::kWrongDescriptorCount: reason = "descriptor count != 1"; break;
            case ivanna::ipc::ReceiveFdResult::kOk: break;
        }
        if (receive_result == ivanna::ipc::ReceiveFdResult::kRecvError) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "recvmsg SCM_RIGHTS fallo errno=%d (%s)",
                                recv_errno, strerror(recv_errno));
        } else {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "respuesta SCM_RIGHTS rechazada: %s", reason);
        }
        return -1;
    }
    return received_fd;
}

extern "C"
OmegaSharedState* omega_daemon_get_shared_state() {
    if (auto* shared = g_shared.load(std::memory_order_acquire)) return shared;

    // audioRouteBridgeLoop may call this every 30 ms; serialize initial mapping
    // and avoid repeated socket handshakes while a mapping is being established.
    if (g_connecting.test_and_set(std::memory_order_acquire)) return nullptr;
    struct ClearFlag {
        ~ClearFlag() { g_connecting.clear(std::memory_order_release); }
    } clear_flag;

    if (auto* shared = g_shared.load(std::memory_order_acquire)) return shared;

    const int fd = receiveDaemonShmFd();
    if (fd < 0) return nullptr;

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        const int stat_errno = errno;
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "fstat descriptor SHM fallo errno=%d (%s)",
                            stat_errno, strerror(stat_errno));
        close(fd);
        return nullptr;
    }
    if (st.st_size < static_cast<off_t>(ivanna::SHM_SIZE)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "descriptor SHM demasiado pequeno size=%lld esperado_min=%zu",
                            static_cast<long long>(st.st_size), ivanna::SHM_SIZE);
        close(fd);
        return nullptr;
    }

    g_map_size = ivanna::SHM_SIZE;
    g_map = mmap(nullptr, g_map_size, PROT_READ, MAP_SHARED, fd, 0);
    const int map_errno = (g_map == MAP_FAILED) ? errno : 0;
    close(fd);

    if (g_map == MAP_FAILED) {
        g_map = nullptr;
        g_map_size = 0;
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "mmap descriptor SHM fallo errno=%d (%s)",
                            map_errno, strerror(map_errno));
        return nullptr;
    }

    if (!ivanna::validateShmHeader(g_map, g_map_size)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "SHM header invalido via descriptor (magic/version/state_size)");
        munmap(g_map, g_map_size);
        g_map = nullptr;
        g_map_size = 0;
        return nullptr;
    }

    auto* header = reinterpret_cast<ivanna::ShmHeader*>(g_map);
    auto* state = reinterpret_cast<OmegaSharedState*>(
        static_cast<uint8_t*>(g_map) + ivanna::SHM_STATE_OFFSET);

    g_shared.store(state, std::memory_order_release);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "OmegaSharedState conectado via SCM_RIGHTS epoch=%llu size=%zu",
                        static_cast<unsigned long long>(header->epoch.load()),
                        g_map_size);
    return g_shared;
}
