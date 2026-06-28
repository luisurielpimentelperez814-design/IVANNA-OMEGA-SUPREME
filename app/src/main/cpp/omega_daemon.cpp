/*
 * IVANNA-FUSION / Ω_in
 * omega_daemon.cpp — Wrapper JNI del daemon OPTIMIZADO (QUIRÚRGICO)
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
#include <cstdlib>
#include <cmath>
#include <thread>
#include <chrono>
#include <string>
#include <cerrno>
#include <sys/select.h>

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

// Buffer alineado para SIMD
alignas(64) float g_process_buf[OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS];
std::atomic<int>  g_complexity_level{0};

// ── Térmica optimizada ───────────────────────────────────────────────────────
__attribute__((hot))
float readBatteryTemperatureC() {
    static FILE* f_cached = nullptr;
    if (!f_cached) {
        f_cached = fopen("/sys/class/power_supply/battery/temp", "r");
        if (!f_cached) return g_shared ? g_shared->current_temperature.load(std::memory_order_relaxed) : 35.0f;
    }
    rewind(f_cached);
    int raw = 0;
    if (fscanf(f_cached, "%d", &raw) != 1) { 
        fclose(f_cached); 
        f_cached = nullptr;
        return 35.0f; 
    }
    return raw * 0.1f;  // Multiplicación más rápida que división
}

__attribute__((hot))
void updateThermalState() {
    float t = readBatteryTemperatureC();
    if (g_shared) g_shared->current_temperature.store(t, std::memory_order_relaxed);
    // Branchless thermal level calculation
    int level = (t >= kThermalLimitC + 5.0f) ? 2 : (t >= kThermalLimitC) ? 1 : 0;
    g_complexity_level.store(level, std::memory_order_relaxed);
}

// ── Modelo (passthrough optimizado) ──────────────────────────────────────────
__attribute__((hot, flatten))
bool loadModel(const std::string& path) {
    // Placeholder: la carga real se implementará con ExecuTorch
    if (access(path.c_str(), F_OK) != 0) {
        LOGE("loadModel: %s no existe", path.c_str());
        return false;
    }
    LOGI("loadModel: registrado %s", path.c_str());
    return true;
}

// Optimizado con __restrict__ para vectorización
__attribute__((hot, flatten))
void runInference(const float* __restrict__ in, float* __restrict__ out, int n) {
    // Bypass ultrarrápido si estamos en throttling
    if (__builtin_expect(g_complexity_level.load(std::memory_order_relaxed) >= 2, 0)) {
        std::memcpy(out, in, n * sizeof(float));
        return;
    }
    // Ruta normal (placeholder para el engine real)
    std::memcpy(out, in, n * sizeof(float));  // La copia optimizada del compilador es lo mejor
}

// ── Afinidad CPU con fallback ────────────────────────────────────────────────
void pinThreadToBigCore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    // Asumimos P‑cores en 4‑7 (ajustable por dispositivo)
    for (int c = 4; c <= 7; ++c) CPU_SET(c, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
        LOGE("sched_setaffinity falló (errno=%d)", errno);
    else
        LOGI("Hilo fijado a cores 4-7 (P-cores)");
    
    // Prioridad máxima para el hilo de audio – si no tenemos privilegios, usamos SCHED_OTHER con nice -20
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        LOGI("SCHED_FIFO no disponible, usando SCHED_OTHER + renice");
        setpriority(PRIO_PROCESS, 0, -20);
    }
}

// ── Memoria compartida ───────────────────────────────────────────────────────
bool init_shared_memory() {
    g_shm_fd = memfd_create_compat("omega_fusion_shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (g_shm_fd < 0) {
        LOGE("memfd_create falló (errno=%d)", errno);
        return false;
    }
    if (ftruncate(g_shm_fd, (off_t)sizeof(OmegaSharedState)) < 0) {
        LOGE("ftruncate falló");
        close(g_shm_fd);
        g_shm_fd = -1;
        return false;
    }
    void* mapped = mmap(nullptr, sizeof(OmegaSharedState),
                        PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (mapped == MAP_FAILED) {
        LOGE("mmap falló");
        close(g_shm_fd);
        g_shm_fd = -1;
        return false;
    }
    g_shared = static_cast<OmegaSharedState*>(mapped);
    new (g_shared) OmegaSharedState();
    LOGI("SHM inicializada (memfd=%d, size=%zu)", g_shm_fd, sizeof(OmegaSharedState));
    return true;
}

void cleanup_shared_memory() {
    if (g_shared) {
        munmap(g_shared, sizeof(OmegaSharedState));
        g_shared = nullptr;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
}

// ── SCM_RIGHTS ───────────────────────────────────────────────────────────────
__attribute__((hot))
bool send_shm_fd(int client_fd) {
    char data = 0;
    struct iovec iov = { &data, 1 };
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &g_shm_fd, sizeof(int));
    return sendmsg(client_fd, &msg, MSG_NOSIGNAL) >= 0;
}

// ── Bucle de audio OPTIMIZADO ────────────────────────────────────────────────
__attribute__((hot, flatten))
void process_audio_thread() {
    pinThreadToBigCore();
    LOGI("Hilo de procesamiento iniciado");
    
    constexpr int samples = OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS;
    int thermal_check_counter = 0;
    
    // Puntero local para evitar accesos repetidos a global
    auto* shared = g_shared;
    if (!shared) return;

    while (__builtin_expect(g_running.load(std::memory_order_relaxed), 1)) {
        // Check rápido de bypass/processing
        if (__builtin_expect(!shared->is_processing.load(std::memory_order_relaxed) || 
                            shared->bypass_enabled.load(std::memory_order_relaxed), 0)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        // Verificación térmica cada 200 iteraciones
        if (++thermal_check_counter >= 200) {
            thermal_check_counter = 0;
            updateThermalState();
            
            if (shared->ai_auto_adapt.load(std::memory_order_relaxed)) {
                float t = shared->current_temperature.load(std::memory_order_relaxed);
                float lat = shared->current_latency_ms.load(std::memory_order_relaxed);
                float sens = shared->ai_sensitivity.load(std::memory_order_relaxed);
                
                if (t > 42.0f) {
                    float red = (t - 42.0f) * 0.1f * sens * 0.005f;
                    float current = shared->intensity.load(std::memory_order_relaxed);
                    shared->intensity.store(std::fmaxf(0.2f, current - red), std::memory_order_relaxed);
                }
                
                // Hysteresis para bypass
                if (lat > 25.0f) {
                    shared->bypass_enabled.store(true, std::memory_order_relaxed);
                } else if (lat < 10.0f && shared->bypass_enabled.load(std::memory_order_relaxed)) {
                    shared->bypass_enabled.store(false, std::memory_order_relaxed);
                }
            }
        }

        // Pop del ring buffer con sleep más corto
        if (!shared->ring_in.tryPop(g_process_buf, samples, &shared->input_buffer[0][0])) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Procesamiento con timing
        auto t0 = std::chrono::steady_clock::now();
        runInference(g_process_buf, g_process_buf, samples);
        auto t1 = std::chrono::steady_clock::now();
        
        float latency_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        shared->current_latency_ms.store(latency_ms, std::memory_order_relaxed);
        
        // Push al ring buffer de salida (ignoramos si está lleno – no debería ocurrir)
        shared->ring_out.tryPush(g_process_buf, samples, &shared->output_buffer[0][0]);
    }
    LOGI("Hilo de procesamiento detenido");
}

// ── Comandos del socket ──────────────────────────────────────────────────────
__attribute__((hot))
void handleClientCommand(const std::string& cmd) {
    auto starts = [&](const char* p) { return cmd.rfind(p, 0) == 0; };
    
    if (!g_shared) return;
    
    if (starts("SET_PROCESSING:")) {
        g_shared->is_processing.store(cmd.back() == '1', std::memory_order_relaxed);
    } else if (starts("SET_INTENSITY:")) {
        g_shared->intensity.store(strtof(cmd.c_str() + 14, nullptr), std::memory_order_relaxed);
    } else if (starts("SET_VOCODER_MIX:")) {
        g_shared->vocoder_mix.store(strtof(cmd.c_str() + 16, nullptr), std::memory_order_relaxed);
    } else if (starts("SET_BYPASS:")) {
        g_shared->bypass_enabled.store(cmd.back() == '1', std::memory_order_relaxed);
    } else if (starts("SET_THERMAL_THROTTLE:")) {
        if (cmd.back() == '1') g_complexity_level.store(2, std::memory_order_relaxed);
    } else if (starts("RESET_DEFAULTS")) {
        g_shared->intensity.store(0.8f, std::memory_order_relaxed);
        g_shared->vocoder_mix.store(0.8f, std::memory_order_relaxed);
        g_shared->bypass_enabled.store(false, std::memory_order_relaxed);
    } else if (starts("SET_PRESET:")) {
        LOGI("SET_PRESET: %s", cmd.c_str() + 11);
    } else if (starts("SET_AI_ENABLED:")) {
        g_shared->ai_enabled.store(cmd.back() == '1', std::memory_order_relaxed);
    } else if (starts("SET_AI_AUTO_ADAPT:")) {
        g_shared->ai_auto_adapt.store(cmd.back() == '1', std::memory_order_relaxed);
    } else if (starts("SET_AI_SENSITIVITY:")) {
        g_shared->ai_sensitivity.store(strtof(cmd.c_str() + 19, nullptr), std::memory_order_relaxed);
    }
}

// Buffer de telemetría (sin alineación innecesaria)
static char telemetry_buf[256];

__attribute__((hot))
const char* buildTelemetryResponse() {
    float temp = g_shared ? g_shared->current_temperature.load(std::memory_order_relaxed) : 0.0f;
    float lat = g_shared ? g_shared->current_latency_ms.load(std::memory_order_relaxed) : 0.0f;
    float rms = g_shared ? g_shared->ai_rms_level.load(std::memory_order_relaxed) : 0.0f;
    float gain_db = g_shared ? g_shared->ai_gain_db.load(std::memory_order_relaxed) : 0.0f;
    
    snprintf(telemetry_buf, sizeof(telemetry_buf),
             "{\"temp\":%.1f,\"npu\":0.0,\"latency\":%.2f,\"complexity_level\":%d,"
             "\"ai_rms\":%.4f,\"ai_gain_db\":%.2f}\n",
             temp, lat, g_complexity_level.load(std::memory_order_relaxed), rms, gain_db);
    return telemetry_buf;
}

// ── Servidor de socket OPTIMIZADO con select() ───────────────────────────────
void socket_server_thread() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("socket() falló");
        return;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, kSocketName, sizeof(addr.sun_path) - 2);
    socklen_t addrlen = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(kSocketName));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), addrlen) < 0) {
        LOGE("bind() falló en '%s' (errno=%d)", kSocketName, errno);
        close(fd);
        return;
    }
    if (listen(fd, 8) < 0) {
        LOGE("listen() falló");
        close(fd);
        return;
    }

    g_socket_fd = fd;
    LOGI("Socket escuchando en '%s'", kSocketName);

    while (g_running.load(std::memory_order_relaxed)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms timeout
        
        int ready = select(fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;
        
        int client = accept(fd, nullptr, nullptr);
        if (client < 0) {
            if (!g_running.load(std::memory_order_relaxed)) break;
            continue;
        }

        // Enviar fd de shm
        send_shm_fd(client);

        // Loop de comandos con select para non-blocking read
        char line[256];
        while (g_running.load(std::memory_order_relaxed)) {
            FD_ZERO(&readfds);
            FD_SET(client, &readfds);
            
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;  // 50ms timeout
            ready = select(client + 1, &readfds, nullptr, nullptr, &timeout);
            
            if (ready <= 0) {
                if (!g_running.load(std::memory_order_relaxed)) break;
                continue;
            }
            
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

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeStart(JNIEnv*, jobject) {
    if (g_running.load(std::memory_order_relaxed)) {
        LOGI("Daemon ya corriendo");
        return JNI_TRUE;
    }
    if (!init_shared_memory()) return JNI_FALSE;
    loadModel("/data/data/com.ivannafusion/files/omega_engine_fp16.pte");
    g_running.store(true, std::memory_order_release);
    g_process_thread = std::thread(process_audio_thread);
    g_socket_thread = std::thread(socket_server_thread);
    LOGI("Daemon Omega iniciado");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeStop(JNIEnv*, jobject) {
    if (!g_running.load(std::memory_order_relaxed)) return;
    g_running.store(false, std::memory_order_release);
    if (g_socket_fd >= 0) shutdown(g_socket_fd, SHUT_RDWR);
    if (g_process_thread.joinable()) g_process_thread.join();
    if (g_socket_thread.joinable()) g_socket_thread.join();
    cleanup_shared_memory();
    LOGI("Daemon Omega detenido");
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeSetProcessing(JNIEnv*, jobject, jboolean v) {
    if (g_shared) g_shared->is_processing.store(v, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeSetIntensity(JNIEnv*, jobject, jfloat v) {
    if (g_shared) g_shared->intensity.store(v, std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeGetTemperature(JNIEnv*, jobject) {
    return g_shared ? g_shared->current_temperature.load(std::memory_order_relaxed) : 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_OmegaDaemon_nativeGetLatency(JNIEnv*, jobject) {
    return g_shared ? g_shared->current_latency_ms.load(std::memory_order_relaxed) : 0.0f;
}

} // extern "C"
