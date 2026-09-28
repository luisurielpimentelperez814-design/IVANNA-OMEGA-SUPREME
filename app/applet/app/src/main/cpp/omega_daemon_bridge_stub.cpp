#include <android/log.h>
#include <atomic>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "include/omega_shared.h"
#include "daemon/core/shm_manager.h"

#define LOG_TAG "OmegaDaemonBridge"

static OmegaSharedState* g_shared = nullptr;
static void* g_map = nullptr;
static size_t g_map_size = 0;

extern "C"
OmegaSharedState* omega_daemon_get_shared_state() {

    if (g_shared)
        return g_shared;

    static std::atomic<bool> warned{false};

    const char* path =
        "/data/adb/ivanna_omega/omega_shm";

    int fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0) {

        bool expected = false;

        if (warned.compare_exchange_strong(expected, true)) {
            __android_log_print(
                ANDROID_LOG_INFO,
                LOG_TAG,
                "SHM no disponible: %s",
                path
            );
        }

        return nullptr;
    }

    g_map_size = ivanna::SHM_SIZE; // constante unica (era literal 65536 desincronizado)

    g_map = mmap(
        nullptr,
        g_map_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    close(fd);

    if (g_map == MAP_FAILED) {

        g_map = nullptr;

        __android_log_print(
            ANDROID_LOG_ERROR,
            LOG_TAG,
            "mmap omega_shm fallo"
        );

        return nullptr;
    }

    // FASE 5: validar el header ANTES de reinterpretar el mmap como estado
    // vivo. Un backing viejo (actualización Magisk con app sin actualizar),
    // truncado o corrupto se rechaza aquí en vez de devolver un puntero a
    // basura que el hot path DSP leería como telemetría real.
    if (!ivanna::validateShmHeader(g_map, g_map_size)) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            LOG_TAG,
            "SHM con header invalido (magic/version/state_size) — rechazando mapeo"
        );
        munmap(g_map, g_map_size);
        g_map = nullptr;
        g_map_size = 0;
        return nullptr;
    }

    auto* header =
        reinterpret_cast<ivanna::ShmHeader*>(g_map);

    auto* state =
        reinterpret_cast<OmegaSharedState*>(
            static_cast<uint8_t*>(g_map)
            + ivanna::SHM_STATE_OFFSET // mismo offset que el daemon (era sizeof(ShmHeader): desalineado)
        );

    g_shared = state;

    __android_log_print(
        ANDROID_LOG_INFO,
        LOG_TAG,
        "OmegaSharedState SHM conectado epoch=%llu",
        (unsigned long long)header->epoch.load()
    );

    return g_shared;
}
