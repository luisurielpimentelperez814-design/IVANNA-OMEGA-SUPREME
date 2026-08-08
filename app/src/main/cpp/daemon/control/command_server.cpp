// daemon/control/command_server.cpp
// Servidor de comandos del daemon IVANNA-OMEGA.
//
// Flujo:
//   1. start() vincula @omega_command_socket (abstract Unix socket).
//   2. acceptLoop() acepta conexiones y despacha el último frame SHM al cliente.
//   3. dispatchFrame() escribe datos en la región SHM mediante OmegaShmManager.
//
// Thread model: start() debe llamarse desde el hilo principal del daemon.
//               acceptLoop() puede correrse en un hilo separado.

#include "command_server.h"
#include "../core/shm_manager.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <android/log.h>

#define CS_TAG "IVANNA_CMD"
#define CS_LOG(fmt, ...) \
    __android_log_print(ANDROID_LOG_INFO, CS_TAG, fmt, ##__VA_ARGS__)

static bool sendall(int fd, const void* data, size_t len)
{
    const uint8_t* ptr = static_cast<const uint8_t*>(data);

    while (len > 0) {
        ssize_t sent = send(fd, ptr, len, MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EINTR)
                continue;

            return false;
        }

        ptr += sent;
        len -= static_cast<size_t>(sent);
    }

    return true;
}



bool CommandServer::start(const std::string& socketName)
{
    serverFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (serverFd < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    if (socketName[0] == '@') {
        addr.sun_path[0] = '\0';
        strncpy(addr.sun_path + 1, socketName.c_str() + 1,
                sizeof(addr.sun_path) - 2);
    } else {
        strncpy(addr.sun_path, socketName.c_str(), sizeof(addr.sun_path) - 1);
    }

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        CS_LOG("bind(%s) error: %s", socketName.c_str(), strerror(errno));
        close(serverFd);
        serverFd = -1;
        return false;
    }

    if (listen(serverFd, 8) != 0) {
        CS_LOG("listen error: %s", strerror(errno));
        close(serverFd);
        serverFd = -1;
        return false;
    }

    CS_LOG("CommandServer activo en %s", socketName.c_str());
    return true;
}

void CommandServer::stop()
{
    if (serverFd >= 0) {
        close(serverFd);
        serverFd = -1;
    }
}

// ── FIX: dispatch SHM frame al cliente conectado ──────────────────────────
// Escribe @p len bytes de @p data en la región SHM via seqlock y opcionalmente
// notifica al cliente por el socket enviando la longitud del frame (4 bytes).
// Retorna true si el write SHM tuvo éxito (independiente del send al socket).
bool CommandServer::dispatchFrame(const void* data, size_t len)
{
    if (!ivanna::shmManager().isReady()) {
        CS_LOG("dispatchFrame: SHM no inicializado");
        return false;
    }
    bool ok = ivanna::shmManager().write(data, len);
    if (!ok) CS_LOG("dispatchFrame: write SHM falló (len=%zu)", len);
    return ok;
}

// ── Accept loop — corre en hilo separado ─────────────────────────────────
// Acepta una conexión, envía 4 bytes (frame_len del SHM) y cierra.
// El cliente (app Kotlin / ShmManager.kt) lee el frame directamente del SHM.
void CommandServer::acceptLoop()
{
    while (serverFd >= 0) {
        int clientFd = accept4(serverFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;  // serverFd cerrado → stop()
        }
        // Notificar frame disponible: enviar frame_len (4 bytes) + epoch (8 bytes)
        if (ivanna::shmManager().isReady()) {
            auto* hdr = static_cast<const ivanna::ShmHeader*>(
                ivanna::shmManager().base());
            uint8_t notify[12]{};
            uint32_t flen = hdr->frame_len;
            uint64_t epoch = hdr->epoch.load(std::memory_order_acquire);
            memcpy(notify,     &flen,  4);
            memcpy(notify + 4, &epoch, 8);
            if (!sendall(clientFd, notify, sizeof(notify))) {
                CS_LOG("sendall notify fallo");
            }
        }
        close(clientFd);
    }
    CS_LOG("acceptLoop terminado");
}
