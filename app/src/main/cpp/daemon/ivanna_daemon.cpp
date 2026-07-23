/**
 * ivanna_daemon.cpp — IVANNA OMEGA SUPREME system daemon
 *
 * Standalone binary deployed by the Magisk module.
 * Runs as root, manages the shared memory ring and
 * routes audio frames from AudioFlinger to libomega_effect.so
 * via the OmegaSharedState IPC channel.
 *
 * Build target: arm64-v8a PIE executable (see build.yml)
 * Deployed to: /system/bin/ivanna_daemon  (via Magisk service.sh)
 */

#include <android/log.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <atomic>
#include <cstring>
#include <cstdlib>

#define LOG_TAG "ivanna_daemon"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ─── Shared memory config (must match omega_daemon.cpp / omega_effect.cpp) ───
static constexpr const char* SHM_NAME      = "/omega_shm";
static constexpr size_t      SHM_SIZE      = 4096;
static constexpr uint32_t    DAEMON_MAGIC  = 0x4F4D4741U; // "OMGA"

struct OmegaSharedState {
    uint32_t  magic;                   // DAEMON_MAGIC when initialized
    uint32_t  version;                 // protocol version
    float     ai_runtime_gain_mul;     // adaptive gain multiplier (1.0 = unity)
    float     target_gain;            // from AdaptiveDecisionEngine
    float     compressor_amount;      // 0.0–1.0
    float     exciter_reduction;      // 0.0–1.0
    float     spatial_width;          // 0.0–1.0
    uint32_t  seq;                     // seqlock counter
    uint8_t   _pad[SHM_SIZE - 7 * sizeof(float) - 3 * sizeof(uint32_t)];
};
static_assert(sizeof(OmegaSharedState) <= SHM_SIZE, "OmegaSharedState exceeds SHM_SIZE");

static std::atomic<bool> g_running{true};

static void sig_handler(int /*sig*/) {
    g_running.store(false, std::memory_order_relaxed);
}

static OmegaSharedState* open_shm() {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0660);
    if (fd < 0) {
        LOGE("shm_open failed: %s", strerror(errno));
        return nullptr;
    }
    if (ftruncate(fd, SHM_SIZE) < 0) {
        LOGE("ftruncate failed: %s", strerror(errno));
        close(fd);
        return nullptr;
    }
    void* ptr = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        LOGE("mmap failed: %s", strerror(errno));
        return nullptr;
    }
    return static_cast<OmegaSharedState*>(ptr);
}

int main(int /*argc*/, char** /*argv*/) {
    LOGI("IVANNA OMEGA SUPREME daemon starting (pid=%d)", getpid());

    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);

    OmegaSharedState* shm = open_shm();
    if (!shm) {
        LOGE("Failed to open shared memory — exiting");
        return 1;
    }

    // Zero-initialise then write magic so the effect lib knows we're up
    memset(shm, 0, SHM_SIZE);
    shm->magic              = DAEMON_MAGIC;
    shm->version            = 2;
    shm->ai_runtime_gain_mul = 1.0f;  // CRITICAL: must not be 0.0 at startup
    shm->target_gain        = 1.0f;
    shm->compressor_amount  = 0.0f;
    shm->exciter_reduction  = 0.0f;
    shm->spatial_width      = 0.5f;
    __sync_synchronize();

    LOGI("Shared memory initialised at %s (size=%zu)", SHM_NAME, SHM_SIZE);
    LOGI("ai_runtime_gain_mul initialised to %.2f (non-zero guard active)", shm->ai_runtime_gain_mul);

    // Main daemon loop — stays alive while Magisk module is loaded
    while (g_running.load(std::memory_order_relaxed)) {
        // Heartbeat: log state every 30 s for logcat diagnostics
        sleep(30);
        if (g_running.load(std::memory_order_relaxed)) {
            LOGD("heartbeat | gain=%.3f comp=%.3f exc=%.3f spatial=%.3f",
                 shm->ai_runtime_gain_mul,
                 shm->compressor_amount,
                 shm->exciter_reduction,
                 shm->spatial_width);
        }
    }

    LOGI("IVANNA daemon shutting down gracefully");
    munmap(shm, SHM_SIZE);
    shm_unlink(SHM_NAME);
    return 0;
}
