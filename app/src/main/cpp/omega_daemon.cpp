/*
 * IVANNA-FUSION / Ω_in
 * omega_daemon.cpp — Wrapper JNI del daemon (librería cargada por la APK)
 *
 * Este archivo es la interfaz JNI que la APK usa para arrancar y controlar
 * el daemon de audio cuando NO hay módulo Magisk instalado (modo fallback),
 * o como complemento de diagnóstico cuando sí lo hay.
 *
 * La memoria compartida usa el mismo mecanismo que omega_daemon_main.cpp:
 * memfd_create + SCM_RIGHTS. El JNI wrapper crea su propio memfd y lo
 * expone a través del socket abstracto igual que el standalone.
 */

#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>
#include <string>
#include <cerrno>

#include "omega_shared.h"

#define LOG_TAG "OmegaDaemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
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

constexpr const char* kSocketName    = "omega_daemon_socket";
constexpr float       kThermalLimitC = 42.0f;

OmegaSharedState* g_shared    = nullptr;
int               g_shm_fd    = -1;
std::atomic<bool> g_running{false};
std::thread       g_process_thread;
std::thread       g_socket_thread;
int               g_socket_fd = -1;

float g_process_buf[OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS];
std::atomic<int>  g_complexity_level{0};

// ── Térmica ──────────────────────────────────────────────────────────────────
float readBatteryTemperatureC() {
    FILE* f = fopen("/sys/class/power_supply/battery/temp", "r");
    if (!f) return g_shared ? g_shared->current_temperature.load() : 35.0f;
    int raw = 0;
    if (fscanf(f, "%d", &raw) != 1) { fclose(f); return 35.0f; }
    fclose(f);
    return raw / 10.0f;
}

void updateThermalState() {
    float t = readBatteryTemperatureC();
    if (g_shared) g_shared->current_temperature.store(t);
    g_complexity_level.store(t >= kThermalLimitC + 5.0f ? 2 : t >= kThermalLimitC ? 1 : 0);
}

// ── Modelo (passthrough hasta que haya .pte) ──────────────────────────────────
bool g_model_loaded = false;

bool loadModel(const std::string& path) {
    if (access(path.c_str(), F_OK) != 0) {
        LOGE("loadModel: %s no existe", path.c_str());
        return false;
    }
    g_model_loaded = true;
    LOGI("loadModel: registrado %s (ExecuTorch no enlazado todavía)", path.c_str());
    return true;
}

void runInference(const float* in, float* out, int n) {
    (void)g_model_loaded;
    if (g_complexity_level.load() >= 2) { std::memcpy(out, in, n * sizeof(float)); return; }
    std::memcpy(out, in, n * sizeof(float));
}

// ── Afinidad CPU — SM4450 big cores (A78) en slots 4-7 ────────────────────────
void pinThreadToBigCore() {
    cpu_set_t cpuset; CPU_ZERO(&cpuset);
    for (int c = 4; c <= 7; ++c) CPU_SET(c, &cpuset);
    int rc = sched_setaffinity(0, sizeof(cpuset), &cpuset);
    if (rc != 0) LOGE("sched_setaffinity falló (errno=%d)", errno);
    else          LOGI("Hilo de inferencia fijado a cores 4-7 (SM4450 P-cores)");
}

// ── Memoria compartida (memfd) ────────────────────────────────────────────────
bool init_shared_memory() {
    g_shm_fd = memfd_create_compat("omega_fusion_shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (g_shm_fd < 0) {
        LOGE("memfd_create falló (errno=%d)", errno);
        return false;
    }
    if (ftruncate(g_shm_fd, (off_t)sizeof(OmegaSharedState)) < 0) {
        LOGE("ftruncate falló");
        close(g_shm_fd); g_shm_fd = -1;
        return false;
    }
    void* mapped = mmap(nullptr, sizeof(OmegaSharedState),
                        PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (mapped == MAP_FAILED) {
        LOGE("mmap falló");
        close(g_shm_fd); g_shm_fd = -1;
        return false;
    }
    g_shared = static_cast<OmegaSharedState*>(mapped);
    new (g_shared) OmegaSharedState();
    LOGI("Memoria compartida inicializada (memfd=%d, size=%zu)", g_shm_fd, sizeof(OmegaSharedState));
    return true;
}

void cleanup_shared_memory() {
    if (g_shared) { munmap(g_shared, sizeof(OmegaSharedState)); g_shared = nullptr; }
    if (g_shm_fd >= 0) { close(g_shm_fd); g_shm_fd = -1; }
}

// ── SCM_RIGHTS: enviar fd al cliente ─────────────────────────────────────────
bool send_shm_fd(int client_fd) {
    char data = 0;
    struct iovec iov = { &data, 1 };
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &g_shm_fd, sizeof(int));
    return sendmsg(client_fd, &msg, 0) >= 0;
}

// ── Bucle de audio ────────────────────────────────────────────────────────────
void process_audio_thread() {
    pinThreadToBigCore();
    LOGI("Hilo de procesamiento iniciado");
    const int samples = OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS;
    int thermal_check_counter = 0;

    while (g_running.load()) {
        if (!g_shared->is_processing.load() || g_shared->bypass_enabled.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (++thermal_check_counter >= 200) {
            thermal_check_counter = 0;
            updateThermalState();
            if (g_shared && g_shared->ai_auto_adapt.load()) {
                float t    = g_shared->current_temperature.load();
                float lat  = g_shared->current_latency_ms.load();
                float sens = g_shared->ai_sensitivity.load();
                if (t > 42.0f) {
                    float red = (t - 42.0f) / 10.0f * sens * 0.005f;
                    g_shared->intensity.store(std::fmaxf(0.2f, g_shared->intensity.load() - red));
                }
                if (lat > 25.0f) g_shared->bypass_enabled.store(true);
                else if (lat < 10.0f && g_shared->bypass_enabled.load())
                    g_shared->bypass_enabled.store(false);
            }
        }

        if (!g_shared->ring_in.tryPop(g_process_buf, samples, &g_shared->input_buffer[0][0])) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
        auto t0 = std::chrono::steady_clock::now();
        runInference(g_process_buf, g_process_buf, samples);
        auto t1 = std::chrono::steady_clock::now();
        g_shared->current_latency_ms.store(
            std::chrono::duration<float, std::milli>(t1 - t0).count());
        g_shared->ring_out.tryPush(g_process_buf, samples, &g_shared->output_buffer[0][0]);
    }
    LOGI("Hilo de procesamiento detenido");
}

// ── Comandos del socket ───────────────────────────────────────────────────────
void handleClientCommand(const std::string& cmd) {
    auto starts = [&](const char* p) { return cmd.rfind(p, 0) == 0; };
    if      (starts("SET_PROCESSING:"))      { if (g_shared) g_shared->is_processing.store(cmd.back()=='1'); }
    else if (starts("SET_INTENSITY:"))       { if (g_shared) g_shared->intensity.store(strtof(cmd.c_str()+14,nullptr)); }
    else if (starts("SET_VOCODER_MIX:"))     { if (g_shared) g_shared->vocoder_mix.store(strtof(cmd.c_str()+16,nullptr)); }
    else if (starts("SET_BYPASS:"))          { if (g_shared) g_shared->bypass_enabled.store(cmd.back()=='1'); }
    else if (starts("SET_THERMAL_THROTTLE:")){ if (cmd.back()=='1') g_complexity_level.store(2); }
    else if (starts("RESET_DEFAULTS"))       {
        if (g_shared) { g_shared->intensity.store(0.8f); g_shared->vocoder_mix.store(0.8f); g_shared->bypass_enabled.store(false); }
    }
    else if (starts("SET_PRESET:"))          { LOGI("SET_PRESET: %s", cmd.c_str()+11); }
    else if (starts("SET_AI_ENABLED:"))       { if (g_shared) g_shared->ai_enabled.store(cmd.back()=='1'); }
    else if (starts("SET_AI_AUTO_ADAPT:"))    { if (g_shared) g_shared->ai_auto_adapt.store(cmd.back()=='1'); }
    else if (starts("SET_AI_SENSITIVITY:"))   { if (g_shared) g_shared->ai_sensitivity.store(strtof(cmd.c_str()+19,nullptr)); }
}

std::string buildTelemetryResponse() {
    float temp  = g_shared ? g_shared->current_temperature.load() : 0.0f;
    float lat   = g_shared ? g_shared->current_latency_ms.load()  : 0.0f;
    float rms     = g_shared ? g_shared->ai_rms_level.load() : 0.0f;
    float gain_db = g_shared ? g_shared->ai_gain_db.load()   : 0.0f;
    char buf[240];
    snprintf(buf, sizeof(buf),
             "{\"temp\":%.1f,\"npu\":0.0,\"latency\":%.2f,\"complexity_level\":%d,"
             "\"ai_rms\":%.4f,\"ai_gain_db\":%.2f}\n",
             temp, lat, g_complexity_level.load(), rms, gain_db);
    return std::string(buf);
}

// ── Servidor de socket ────────────────────────────────────────────────────────
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
    LOGI("Unix Domain Socket escuchando en '%s' (namespace abstracto)", kSocketName);

    while (g_running.load()) {
        int client = accept(fd, nullptr, nullptr);
        if (client < 0) { if (!g_running.load()) break; continue; }

        // Enviar fd de shm via SCM_RIGHTS como primer mensaje
        send_shm_fd(client);

        char line[256];
        while (g_running.load()) {
            ssize_t n = read(client, line, sizeof(line) - 1);
            if (n <= 0) break;
            line[n] = '\0';
            std::string cmd(line);
            while (!cmd.empty() && (cmd.back()=='\n'||cmd.back()=='\r')) cmd.pop_back();
            if (cmd.empty()) continue;
            handleClientCommand(cmd);
            if (cmd.rfind("GET_TELEMETRY", 0) == 0) {
                std::string resp = buildTelemetryResponse();
                write(client, resp.c_str(), resp.size());
            }
        }
        close(client);
    }
    close(fd); g_socket_fd = -1;
}

} // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeStart(JNIEnv*, jobject) {
    if (g_running.load()) { LOGI("Daemon ya corriendo"); return JNI_TRUE; }
    if (!init_shared_memory()) return JNI_FALSE;
    loadModel("/data/data/com.ivannafusion/files/omega_engine_fp16.pte");
    g_running.store(true);
    g_process_thread = std::thread(process_audio_thread);
    g_socket_thread  = std::thread(socket_server_thread);
    LOGI("Daemon Omega iniciado");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeStop(JNIEnv*, jobject) {
    if (!g_running.load()) return;
    g_running.store(false);
    if (g_socket_fd >= 0) shutdown(g_socket_fd, SHUT_RDWR);
    if (g_process_thread.joinable()) g_process_thread.join();
    if (g_socket_thread.joinable())  g_socket_thread.join();
    cleanup_shared_memory();
    LOGI("Daemon Omega detenido");
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeSetProcessing(JNIEnv*, jobject, jboolean v) {
    if (g_shared) g_shared->is_processing.store(v);
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeSetIntensity(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->intensity.store(v);
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeGetTemperature(JNIEnv*, jobject) {
    return g_shared ? g_shared->current_temperature.load() : 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeGetLatency(JNIEnv*, jobject) {
    return g_shared ? g_shared->current_latency_ms.load() : 0.0f;
}

} // extern "C"
