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

    char byte = 0;
    iovec io{};
    io.iov_base = &byte;
    io.iov_len = sizeof(byte);
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr msg{};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    const ssize_t n = recvmsg(sock, &msg, MSG_CMSG_CLOEXEC);
    const int recv_errno = errno;
    close(sock);

    if (n != 1 || (msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) != 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "recvmsg SCM_RIGHTS fallo n=%zd flags=0x%x errno=%d (%s)",
                            n, msg.msg_flags, recv_errno, strerror(recv_errno));
        return -1;
    }

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
            cmsg->cmsg_len < CMSG_LEN(sizeof(int))) {
            continue;
        }
        int received_fd = -1;
        std::memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(received_fd));
        if (received_fd >= 0) {
            return received_fd;
        }
    }

    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "daemon respondio sin descriptor SCM_RIGHTS");
    return -1;
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
    if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(ivanna::SHM_SIZE)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "descriptor SHM invalido size=%lld esperado_min=%zu errno=%d",
                            static_cast<long long>(st.st_size), ivanna::SHM_SIZE, errno);
        close(fd);
        return nullptr;
    }

    g_map_size = ivanna::SHM_SIZE;
    g_map = mmap(nullptr, g_map_size, PROT_READ, MAP_SHARED, fd, 0);
    const int map_errno = errno;
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
