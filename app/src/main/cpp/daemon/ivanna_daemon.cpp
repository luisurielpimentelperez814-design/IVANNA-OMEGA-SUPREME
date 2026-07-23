/**
 * ivanna_daemon.cpp — IVANNA OMEGA SUPREME system daemon
 *
 * Standalone binary deployed by the Magisk module.
 * Runs as root, manages the shared memory ring and
 * routes adaptive engine state to libomega_effect.so
 * via the OmegaSharedState IPC channel.
 *
 * Build: aarch64-linux-android35-clang++ -std=c++17 -O3 -fPIE -pie
 *        -Wl,-z,relro,-z,now  -llog  -Wl,--no-as-needed -lc++_shared
 * Deploy: /system/bin/ivanna_daemon  (via Magisk service.sh)
 *
 * FIX CRÍTICO (v2.0): ai_runtime_gain_mul inicializado a 1.0f — no 0.0.
 * OmegaSharedState vive en mmap() crudo; su constructor C++ nunca se
 * ejecuta, el memset(0) dejaba gain=0 → silencio total en streaming.
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

// ── Shared memory config (must match omega_daemon.cpp / omega_effect.cpp) ─────
static constexpr const char* SHM_NAME      = "/omega_shm";
static constexpr size_t      SHM_SIZE      = 4096;
static constexpr uint32_t    DAEMON_MAGIC  = 0x4F4D4741U; // "OMGA"

struct OmegaSharedState {
    uint32_t magic;
    uint32_t version;
    float    ai_runtime_gain_mul;  // ← MUST be 1.0f at startup, never 0.0f
    float    target_gain;
    float    compressor_amount;
    float    exciter_reduction;
    float    spatial_width;
    uint32_t seq;
    uint8_t  _pad[SHM_SIZE - 7 * sizeof(float) - 3 * sizeof(uint32_t)];
};
static_assert(sizeof(OmegaSharedState) <= SHM_SIZE, "OmegaSharedState exceeds SHM_SIZE");

static std::atomic<bool> g_running{true};

static void sig_handler(int /*sig*/) {
    g_running.store(false, std::memory_order_relaxed);
}

static OmegaSharedState* open_shm() {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0660);
    if (fd < 0) { LOGE("shm_open: %s", strerror(errno)); return nullptr; }
    if (ftruncate(fd, static_cast<off_t>(SHM_SIZE)) < 0) {
        LOGE("ftruncate: %s", strerror(errno));
        close(fd); return nullptr;
    }
    void* ptr = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) { LOGE("mmap: %s", strerror(errno)); return nullptr; }
    return static_cast<OmegaSharedState*>(ptr);
}

int main(int /*argc*/, char** /*argv*/) {
    LOGI("IVANNA OMEGA SUPREME daemon starting (pid=%d)", getpid());

    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);

    OmegaSharedState* shm = open_shm();
    if (!shm) { LOGE("Failed to open shared memory — exiting"); return 1; }

    // Zero-init then set critical fields — constructor never runs on mmap memory
    memset(shm, 0, SHM_SIZE);
    shm->magic               = DAEMON_MAGIC;
    shm->version             = 2;
    shm->ai_runtime_gain_mul = 1.0f;   // FIX: NOT 0.0f — would silence all streaming
    shm->target_gain         = 1.0f;
    shm->compressor_amount   = 0.0f;
    shm->exciter_reduction   = 0.0f;
    shm->spatial_width       = 0.5f;
    __sync_synchronize();

    LOGI("SHM initialized: %s  size=%zu  gain=%.2f", SHM_NAME, SHM_SIZE, shm->ai_runtime_gain_mul);

    // Main daemon loop
    while (g_running.load(std::memory_order_relaxed)) {
        sleep(30);
        if (g_running.load(std::memory_order_relaxed)) {
            LOGD("heartbeat | gain=%.3f comp=%.3f exc=%.3f spatial=%.3f",
                 shm->ai_runtime_gain_mul, shm->compressor_amount,
                 shm->exciter_reduction,   shm->spatial_width);
        }
    }

    LOGI("IVANNA daemon shutting down gracefully");
    munmap(shm, SHM_SIZE);
    shm_unlink(SHM_NAME);
    return 0;
}
