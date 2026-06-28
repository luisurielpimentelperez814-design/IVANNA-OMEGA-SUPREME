/*
 * IVANNA-OMEGA-SUPREME
 * omega_daemon.cpp — Daemon de audio con PF Engine real (FIXED)
 *
 * FIXES:
 * 1. Thermal bypass now passes through audio instead of silence
 * 2. Telemetry buffer is thread-local
 * 3. EINTR handling in socket server
 * 4. Memory cleanup on all error paths
 */

#include <aaudio/AAudio.h>
#include <jni.h>
#include <cmath>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <android/log.h>
#include <sched.h>

#include "omega_shared.h"
#include "dsp_types.h"

#define LOG_TAG "OmegaDaemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 2U
#endif

static int memfd_create_compat(const char* name, unsigned int flags) {
    return (int)syscall(__NR_memfd_create, name, flags);
}

namespace {

constexpr const char* kSocketName = "omega_daemon_socket";
constexpr float kThermalLimitC = 42.0f;

OmegaSharedState* g_shared = nullptr;
int g_shm_fd = -1;
std::atomic<bool> g_running{false};
std::thread g_process_thread;
std::thread g_socket_thread;
int g_socket_fd = -1;

alignas(64) float g_process_buf[OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS];
std::atomic<int> g_complexity_level{0};

// ── PF Engine — estado Biquad por canal ──────────────────────────────────────
struct PFEngineState {
    ivanna::Biquad low[2];
    ivanna::Biquad mid[2];
    ivanna::Biquad high[2];
    ivanna::Biquad presence[2];
    uint32_t coeff_version = 0;

    void recompute(const OmegaSharedState* s) {
        double freq = (double)s->pf_freq.load(std::memory_order_relaxed);
        double Q = (double)s->pf_resonance.load(std::memory_order_relaxed);
        double low_g = (double)s->pf_low.load(std::memory_order_relaxed);
        double mid_g = (double)s->pf_mid.load(std::memory_order_relaxed);
        double hi_g = (double)s->pf_high.load(std::memory_order_relaxed);
        double pre_g = (double)s->pf_presence.load(std::memory_order_relaxed);

        if (Q < 0.1) Q = 0.1;
        if (Q > 10.0) Q = 10.0;
        if (freq < 20.0) freq = 20.0;
        if (freq > 18000.0) freq = 18000.0;

        for (int ch = 0; ch < 2; ++ch) {
            low[ch].setLowShelf (200.0, 0.707, low_g, OMEGA_SAMPLE_RATE);
            mid[ch].setPeaking (freq, Q, mid_g, OMEGA_SAMPLE_RATE);
            high[ch].setHighShelf(8000.0, 0.707, hi_g, OMEGA_SAMPLE_RATE);
            presence[ch].setPeaking(6000.0, 2.0, pre_g, OMEGA_SAMPLE_RATE);
        }
        coeff_version = s->pf_param_version.load(std::memory_order_relaxed);
    }
};

static PFEngineState g_pf;

// ── PF Engine — procesamiento real ──────────────────────────────────────────
__attribute__((hot, flatten))
void processPFEngine(float* __restrict__ buf, int frames) {
    if (!g_shared) return;

    uint32_t cur_ver = g_shared->pf_param_version.load(std::memory_order_relaxed);
    if (__builtin_expect(g_pf.coeff_version != cur_ver, 0)) {
        g_pf.recompute(g_shared);
    }

    float drive = g_shared->pf_drive.load(std::memory_order_relaxed);
    float wet = g_shared->pf_wet.load(std::memory_order_relaxed);
    float mix = g_shared->pf_mix.load(std::memory_order_relaxed);
    float master_g = std::exp2f(g_shared->pf_master.load(std::memory_order_relaxed) * 0.1660964f);

    float drive_scale = 1.0f + drive * 4.0f;
    float drive_inv = 1.0f / drive_scale;
    float out_gain = mix * master_g;

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float in = buf[i * 2 + ch];
            float dry = in;

            float driven = std::tanhf(in * drive_scale) * drive_inv;

            driven = g_pf.low[ch].process(driven);
            driven = g_pf.mid[ch].process(driven);
            driven = g_pf.high[ch].process(driven);
            driven = g_pf.presence[ch].process(driven);

            buf[i * 2 + ch] = (dry + (driven - dry) * wet) * out_gain;
        }
    }
}

// ── Térmica ──────────────────────────────────────────────────────────────────
__attribute__((hot))
float readBatteryTemperatureC() {
    static FILE* f_cached = nullptr;
    if (!f_cached) {
        f_cached = fopen("/sys/class/power_supply/battery/temp", "r");
        if (!f_cached)
            return g_shared ? g_shared->current_temperature.load(std::memory_order_relaxed) : 35.0f;
    }
    rewind(f_cached);
    int raw = 0;
    if (fscanf(f_cached, "%d", &raw) != 1) {
        fclose(f_cached);
        f_cached = nullptr;
        return 35.0f;
    }
    return raw * 0.1f;
}

__attribute__((hot))
void updateThermalState() {
    float t = readBatteryTemperatureC();
    if (g_shared) g_shared->current_temperature.store(t, std::memory_order_relaxed);
    int level = (t >= kThermalLimitC + 5.0f) ? 2 : (t >= kThermalLimitC) ? 1 : 0;
    g_complexity_level.store(level, std::memory_order_relaxed);
}

// ── Afinidad CPU ──────────────────────────────────────────────────────────────
void pinThreadToBigCore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int c = 4; c <= 7; ++c) CPU_SET(c, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
        LOGE("sched_setaffinity falló (errno=%d)", errno);
    else
        LOGI("Hilo fijado a cores 4-7 (P-cores)");

    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        LOGI("SCHED_FIFO no disponible, usando nice -20");
        setpriority(PRIO_PROCESS, 0, -20);
    }
}

// ── SHM ──────────────────────────────────────────────────────────────────────
bool init_shared_memory() {
    g_shm_fd = memfd_create_compat("omega_fusion_shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (g_shm_fd < 0) { LOGE("memfd_create falló (errno=%d)", errno); return false; }
    if (ftruncate(g_shm_fd, (off_t)sizeof(OmegaSharedState)) < 0) {
        LOGE("ftruncate falló"); close(g_shm_fd); g_shm_fd = -1; return false;
    }
    void* mapped = mmap(nullptr, sizeof(OmegaSharedState),
        PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (mapped == MAP_FAILED) {
        LOGE("mmap falló"); close(g_shm_fd); g_shm_fd = -1; return false;
    }
    g_shared = static_cast<OmegaSharedState*>(mapped);
    new (g_shared) OmegaSharedState();
    g_pf.recompute(g_shared);
    LOGI("SHM inicializada (memfd=%d, size=%zu)", g_shm_fd, sizeof(OmegaSharedState));
    return true;
}

void cleanup_shared_memory() {
    if (g_shared) { munmap(g_shared, sizeof(OmegaSharedState)); g_shared = nullptr; }
    if (g_shm_fd >= 0) { close(g_shm_fd); g_shm_fd = -1; }
}

// ── SCM_RIGHTS ────────────────────────────────────────────────────────────────
__attribute__((hot))
bool send_shm_fd(int client_fd) {
    char data = 0;
    struct iovec iov = { &data, 1 };
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov = &iov; msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf; msg.msg_controllen = sizeof(cmsgbuf);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &g_shm_fd, sizeof(int));
    return sendmsg(client_fd, &msg, MSG_NOSIGNAL) >= 0;
}

// ── Bucle de procesamiento de audio ──────────────────────────────────────────
__attribute__((hot, flatten))
void process_audio_thread() {
    pinThreadToBigCore();
    LOGI("Hilo de procesamiento iniciado");

    constexpr int samples = OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS;
    int thermal_check_counter = 0;
    auto* shared = g_shared;
    if (!shared) return;

    while (__builtin_expect(g_running.load(std::memory_order_relaxed), 1)) {
        if (__builtin_expect(
            !shared->is_processing.load(std::memory_order_relaxed) ||
            shared->bypass_enabled.load(std::memory_order_relaxed), 0)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (++thermal_check_counter >= 200) {
            thermal_check_counter = 0;
            updateThermalState();

            if (shared->ai_auto_adapt.load(std::memory_order_relaxed)) {
                float t = shared->current_temperature.load(std::memory_order_relaxed);
                float lat = shared->current_latency_ms.load(std::memory_order_relaxed);
                float sens = shared->ai_sensitivity.load(std::memory_order_relaxed);

                if (t > 42.0f) {
                    float red = (t - 42.0f) * 0.1f * sens * 0.005f;
                    float cur = shared->intensity.load(std::memory_order_relaxed);
                    shared->intensity.store(std::fmaxf(0.2f, cur - red), std::memory_order_relaxed);
                }
                if (lat > 25.0f)
                    shared->bypass_enabled.store(true, std::memory_order_relaxed);
                else if (lat < 10.0f && shared->bypass_enabled.load(std::memory_order_relaxed))
                    shared->bypass_enabled.store(false, std::memory_order_relaxed);
            }
        }

        if (!shared->ring_in.tryPop(g_process_buf, samples, &shared->input_buffer[0][0])) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        auto t0 = std::chrono::steady_clock::now();

        // FIX #32: Thermal bypass now copies input to output (passthrough)
        int complexity = g_complexity_level.load(std::memory_order_relaxed);
        if (complexity < 2) {
            processPFEngine(g_process_buf, OMEGA_BLOCK_SIZE);
        }
        // For complexity >= 2: passthrough (buffer already has input data)

        auto t1 = std::chrono::steady_clock::now();
        float latency_ms = std::chrono::duration<float>(t1 - t0).count() * 1000.0f;
        shared->current_latency_ms.store(latency_ms, std::memory_order_relaxed);

        shared->ring_out.tryPush(g_process_buf, samples, &shared->output_buffer[0][0]);
    }
    LOGI("Hilo de procesamiento detenido");
}

// ── Comandos del socket ────────────────────────────────────────────────────────
static inline void bump_pf_version() {
    if (g_shared)
        g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}

__attribute__((hot))
void handleClientCommand(const std::string& cmd) {
    auto starts = [&](const char* p) { return cmd.rfind(p, 0) == 0; };
    if (!g_shared) return;

    if (starts("SET_PROCESSING:")) g_shared->is_processing.store(cmd.back()=='1', std::memory_order_relaxed);
    else if (starts("SET_INTENSITY:")) g_shared->intensity.store(strtof(cmd.c_str()+14, nullptr), std::memory_order_relaxed);
    else if (starts("SET_VOCODER_MIX:")) g_shared->vocoder_mix.store(strtof(cmd.c_str()+16, nullptr), std::memory_order_relaxed);
    else if (starts("SET_BYPASS:")) g_shared->bypass_enabled.store(cmd.back()=='1', std::memory_order_relaxed);
    else if (starts("SET_THERMAL_THROTTLE:")) { if (cmd.back()=='1') g_complexity_level.store(2, std::memory_order_relaxed); }
    else if (starts("SET_AI_ENABLED:")) g_shared->ai_enabled.store(cmd.back()=='1', std::memory_order_relaxed);
    else if (starts("SET_AI_AUTO_ADAPT:"))g_shared->ai_auto_adapt.store(cmd.back()=='1', std::memory_order_relaxed);
    else if (starts("SET_AI_SENSITIVITY:"))g_shared->ai_sensitivity.store(strtof(cmd.c_str()+19, nullptr), std::memory_order_relaxed);
    else if (starts("RESET_DEFAULTS")) {
        g_shared->intensity.store(0.8f, std::memory_order_relaxed);
        g_shared->vocoder_mix.store(0.8f, std::memory_order_relaxed);
        g_shared->bypass_enabled.store(false, std::memory_order_relaxed);
        g_shared->pf_drive.store(0.65f, std::memory_order_relaxed);
        g_shared->pf_wet.store(0.5f, std::memory_order_relaxed);
        g_shared->pf_mix.store(0.7f, std::memory_order_relaxed);
        g_shared->pf_freq.store(1000.f, std::memory_order_relaxed);
        g_shared->pf_resonance.store(0.707f, std::memory_order_relaxed);
        g_shared->pf_low.store(0.f, std::memory_order_relaxed);
        g_shared->pf_mid.store(0.f, std::memory_order_relaxed);
        g_shared->pf_high.store(0.f, std::memory_order_relaxed);
        g_shared->pf_presence.store(0.f, std::memory_order_relaxed);
        g_shared->pf_master.store(0.f, std::memory_order_relaxed);
        bump_pf_version();
    }
    else if (starts("SET_PF_DRIVE:")) { g_shared->pf_drive.store(strtof(cmd.c_str()+12, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PF_WET:")) { g_shared->pf_wet.store(strtof(cmd.c_str()+10, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PF_MIX:")) { g_shared->pf_mix.store(strtof(cmd.c_str()+10, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PF_ALPHA:")) { g_shared->pf_alpha.store(strtof(cmd.c_str()+12, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PF_BETA:")) { g_shared->pf_beta.store(strtof(cmd.c_str()+11, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PF_GAMMA:")) { g_shared->pf_gamma.store(strtof(cmd.c_str()+12, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PF_FREQ:")) { g_shared->pf_freq.store(strtof(cmd.c_str()+12, nullptr), std::memory_order_relaxed); bump_pf_version(); }
    else if (starts("SET_PF_RESONANCE:")){ g_shared->pf_resonance.store(strtof(cmd.c_str()+17, nullptr), std::memory_order_relaxed); bump_pf_version(); }
    else if (starts("SET_PF_LOW:")) { g_shared->pf_low.store(strtof(cmd.c_str()+11, nullptr), std::memory_order_relaxed); bump_pf_version(); }
    else if (starts("SET_PF_MID:")) { g_shared->pf_mid.store(strtof(cmd.c_str()+11, nullptr), std::memory_order_relaxed); bump_pf_version(); }
    else if (starts("SET_PF_HIGH:")) { g_shared->pf_high.store(strtof(cmd.c_str()+12, nullptr), std::memory_order_relaxed); bump_pf_version(); }
    else if (starts("SET_PF_PRESENCE:")) { g_shared->pf_presence.store(strtof(cmd.c_str()+16, nullptr), std::memory_order_relaxed); bump_pf_version(); }
    else if (starts("SET_PF_MASTER:")) { g_shared->pf_master.store(strtof(cmd.c_str()+14, nullptr), std::memory_order_relaxed); }
    else if (starts("SET_PRESET:")) { LOGI("SET_PRESET: %s", cmd.c_str()+11); }
}

// FIX #34: Thread-local telemetry buffer
__attribute__((hot))
const char* buildTelemetryResponse() {
    static thread_local char telemetry_buf[512];
    float temp = g_shared ? g_shared->current_temperature.load(std::memory_order_relaxed) : 0.0f;
    float lat = g_shared ? g_shared->current_latency_ms.load(std::memory_order_relaxed) : 0.0f;
    float rms = g_shared ? g_shared->ai_rms_level.load(std::memory_order_relaxed) : 0.0f;
    float gain = g_shared ? g_shared->ai_gain_db.load(std::memory_order_relaxed) : 0.0f;
    float drive = g_shared ? g_shared->pf_drive.load(std::memory_order_relaxed) : 0.0f;
    snprintf(telemetry_buf, sizeof(telemetry_buf),
        "{\"temp\":%.1f,\"npu\":0.0,\"latency\":%.2f,\"complexity_level\":%d,"
        "\"ai_rms\":%.4f,\"ai_gain_db\":%.2f,\"pf_drive\":%.3f}\n",
        temp, lat, g_complexity_level.load(std::memory_order_relaxed), rms, gain, drive);
    return telemetry_buf;
}

// ── Servidor socket ──────────────────────────────────────────────────────────
void socket_server_thread() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { LOGE("socket() falló"); return; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, kSocketName, sizeof(addr.sun_path) - 2);
    socklen_t addrlen = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(kSocketName));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), addrlen) < 0) {
        LOGE("bind() falló en '%s' (errno=%d)", kSocketName, errno);
        close(fd); return;
    }
    if (listen(fd, 8) < 0) { LOGE("listen() falló"); close(fd); return; }

    g_socket_fd = fd;
    LOGI("Socket escuchando en '%s'", kSocketName);

    while (g_running.load(std::memory_order_relaxed)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval timeout = { 0, 100000 };

        // FIX #33: Handle EINTR
        int ret = select(fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        int client = accept(fd, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            if (!g_running.load(std::memory_order_relaxed)) break;
            continue;
        }
        send_shm_fd(client);

        char line[256];
        while (g_running.load(std::memory_order_relaxed)) {
            FD_ZERO(&readfds);
            FD_SET(client, &readfds);
            timeout = { 0, 50000 };

            ret = select(client + 1, &readfds, nullptr, nullptr, &timeout);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) continue;

            ssize_t n = read(client, line, sizeof(line) - 1);
            if (n <= 0) break;
            line[n] = '\0';
            std::string cmd(line);
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
            if (cmd.empty()) continue;

            handleClientCommand(cmd);

            if (cmd.rfind("GET_TELEMETRY", 0) == 0) {
                const char* resp = buildTelemetryResponse();
                write(client, resp, strlen(resp));
            }
        }
        close(client);
    }
    close(fd);
    g_socket_fd = -1;
}

} // namespace

// ── JNI ── com.ivanna.omega.OmegaDaemon ──────────────────────────────────────
extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeStart(JNIEnv*, jobject) {
    if (g_running.load(std::memory_order_relaxed)) { LOGI("Daemon ya corriendo"); return JNI_TRUE; }
    if (!init_shared_memory()) return JNI_FALSE;
    g_running.store(true, std::memory_order_release);
    g_process_thread = std::thread(process_audio_thread);
    g_socket_thread = std::thread(socket_server_thread);
    LOGI("Daemon Omega iniciado");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeStop(JNIEnv*, jobject) {
    if (!g_running.load(std::memory_order_relaxed)) return;
    g_running.store(false, std::memory_order_release);
    if (g_socket_fd >= 0) shutdown(g_socket_fd, SHUT_RDWR);
    if (g_process_thread.joinable()) g_process_thread.join();
    if (g_socket_thread.joinable()) g_socket_thread.join();
    cleanup_shared_memory();
    LOGI("Daemon Omega detenido");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetProcessing(JNIEnv*, jobject, jboolean v) {
    if (g_shared) g_shared->is_processing.store(v, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetIntensity(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->intensity.store(v, std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeGetTemperature(JNIEnv*, jobject) {
    return g_shared ? g_shared->current_temperature.load(std::memory_order_relaxed) : 0.f;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeGetLatency(JNIEnv*, jobject) {
    return g_shared ? g_shared->current_latency_ms.load(std::memory_order_relaxed) : 0.f;
}

// PF Engine setters
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFDrive(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_drive.store(v, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFWet(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_wet.store(v, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFMix(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_mix.store(v, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFAlpha(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_alpha.store(v, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFBeta(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_beta.store(v, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFGamma(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_gamma.store(v, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFMaster(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->pf_master.store(v, std::memory_order_relaxed);
}

// Params que SÍ afectan coeficientes Biquad
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFFreq(JNIEnv*, jobject, jfloat v) {
    if (!g_shared) return;
    g_shared->pf_freq.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFResonance(JNIEnv*, jobject, jfloat v) {
    if (!g_shared) return;
    g_shared->pf_resonance.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFLow(JNIEnv*, jobject, jfloat v) {
    if (!g_shared) return;
    g_shared->pf_low.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFMid(JNIEnv*, jobject, jfloat v) {
    if (!g_shared) return;
    g_shared->pf_mid.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFHigh(JNIEnv*, jobject, jfloat v) {
    if (!g_shared) return;
    g_shared->pf_high.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFPresence(JNIEnv*, jobject, jfloat v) {
    if (!g_shared) return;
    g_shared->pf_presence.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}

// Bulk setter
JNIEXPORT void JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeSetPFParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma,
    jfloat freq, jfloat resonance,
    jfloat low, jfloat mid, jfloat high,
    jfloat presence, jfloat master) {
    if (!g_shared) return;
    g_shared->pf_drive.store(drive, std::memory_order_relaxed);
    g_shared->pf_wet.store(wet, std::memory_order_relaxed);
    g_shared->pf_mix.store(mix, std::memory_order_relaxed);
    g_shared->pf_alpha.store(alpha, std::memory_order_relaxed);
    g_shared->pf_beta.store(beta, std::memory_order_relaxed);
    g_shared->pf_gamma.store(gamma, std::memory_order_relaxed);
    g_shared->pf_freq.store(freq, std::memory_order_relaxed);
    g_shared->pf_resonance.store(resonance, std::memory_order_relaxed);
    g_shared->pf_low.store(low, std::memory_order_relaxed);
    g_shared->pf_mid.store(mid, std::memory_order_relaxed);
    g_shared->pf_high.store(high, std::memory_order_relaxed);
    g_shared->pf_presence.store(presence, std::memory_order_relaxed);
    g_shared->pf_master.store(master, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_relaxed);
}

// Bulk getter
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_OmegaDaemon_nativeGetPFParams(JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(13);
    if (!arr || !g_shared) return arr;
    float tmp[13] = {
        g_shared->pf_drive.load(std::memory_order_relaxed),
        g_shared->pf_wet.load(std::memory_order_relaxed),
        g_shared->pf_mix.load(std::memory_order_relaxed),
        g_shared->pf_alpha.load(std::memory_order_relaxed),
        g_shared->pf_beta.load(std::memory_order_relaxed),
        g_shared->pf_gamma.load(std::memory_order_relaxed),
        g_shared->pf_freq.load(std::memory_order_relaxed),
        g_shared->pf_resonance.load(std::memory_order_relaxed),
        g_shared->pf_low.load(std::memory_order_relaxed),
        g_shared->pf_mid.load(std::memory_order_relaxed),
        g_shared->pf_high.load(std::memory_order_relaxed),
        g_shared->pf_presence.load(std::memory_order_relaxed),
        g_shared->pf_master.load(std::memory_order_relaxed),
    };
    env->SetFloatArrayRegion(arr, 0, 13, tmp);
    return arr;
}

} // extern "C"
