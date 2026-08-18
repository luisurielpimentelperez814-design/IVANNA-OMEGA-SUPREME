#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <android/log.h>
#include <atomic>
#include "include/omega_shared.h"
#define LOG_TAG "OmegaDaemonBridge"

extern "C" OmegaSharedState* omega_daemon_get_shared_state() {
    static OmegaSharedState* cached = nullptr;
    static std::atomic<bool> warned{false};
    if (cached) return cached;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return nullptr;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = 0; // abstract namespace
    strcpy(addr.sun_path + 1, "omega_daemon_socket");
    socklen_t len = sizeof(sa_family_t) + 1 + strlen("omega_daemon_socket");

    if (connect(sock, (struct sockaddr*)&addr, len)!= 0) {
        close(sock);
        bool expected = false;
        if (warned.compare_exchange_strong(expected, true)) {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "daemon no disponible, durmiendo");
        }
        return nullptr;
    }

    char buf[1];
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {.iov_base = buf,.iov_len = sizeof(buf) };
    struct msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    if (recvmsg(sock, &msg, 0) <= 0) { close(sock); return nullptr; }

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_len!= CMSG_LEN(sizeof(int))) { close(sock); return nullptr; }

    int fd = *(int*)CMSG_DATA(cmsg);
    cached = (OmegaSharedState*) mmap(NULL, sizeof(OmegaSharedState), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(sock);
    close(fd);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "puente memfd mapeado OK");
    return (cached == MAP_FAILED)? nullptr : cached;
}
