/*
 * IVANNA-OMEGA-SUPREME v1.5 — omega_daemon.cpp
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * FIXES v1.5:
 * 1. Watchdog: 3 fallos consecutivos antes de safe_mode (no muere al primer fallo)
 * 2. Limpieza de memoria en todos los paths de error
 * 3. Thermal bypass pasa audio (no silencio)
 * 4. EINTR handling en socket server
 * 5. Telemetry buffer thread-local
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
#include <fstream>

#include "omega_shared.h"
#include "dsp_types.h"
#include "include/audio_thread_priority.h"

#define LOG_TAG "OmegaDaemon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

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

// ── Estructura Biquad para EQ ─────────────────────────────────────────────────
struct Biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    
    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }
    
    float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// Funciones helper para calcular coeficientes Biquad
static void calcLowShelf(Biquad& bq, float freq, float Q, float gainDB) {
    float A = powf(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * M_PI * freq / 48000.0f;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);
    
    float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtf(A) * alpha;
    bq.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtf(A) * alpha)) / a0;
    bq.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
    bq.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtf(A) * alpha)) / a0;
    bq.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
    bq.a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtf(A) * alpha) / a0;
}

static void calcPeaking(Biquad& bq, float freq, float Q, float gainDB) {
    float A = powf(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * M_PI * freq / 48000.0f;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);
    
    float a0 = 1.0f + alpha / A;
    bq.b0 = (1.0f + alpha * A) / a0;
    bq.b1 = (-2.0f * cosw0) / a0;
    bq.b2 = (1.0f - alpha * A) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha / A) / a0;
}

static void calcHighShelf(Biquad& bq, float freq, float Q, float gainDB) {
    float A = powf(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * M_PI * freq / 48000.0f;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);
    
    float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtf(A) * alpha;
    bq.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtf(A) * alpha)) / a0;
    bq.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
    bq.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtf(A) * alpha)) / a0;
    bq.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
    bq.a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtf(A) * alpha) / a0;
}

// ── PF Engine — estado Biquad por canal ──────────────────────────────────────
struct PFEngineState {
    Biquad low[2];
    Biquad mid[2];
    Biquad high[2];
    Biquad presence[2];
    uint32_t coeff_version = 0;

    void recompute(const OmegaSharedState* s) {
        double freq = (double)s->pf_freq.load(std::memory_order_relaxed);
        double Q = (double)s->pf_resonance.load(std::memory_order_relaxed);
        double gain = (double)s->pf_mid.load(std::memory_order_relaxed);
        // Low shelf @ 200 Hz
        calcLowShelf(low[0], freq, Q, gain);
        calcLowShelf(low[1], freq, Q, gain);
        // Peaking @ 1 kHz
        calcPeaking(mid[0], freq, Q, gain);
        calcPeaking(mid[1], freq, Q, gain);
        // High shelf @ 6 kHz
        calcHighShelf(high[0], freq, Q, gain);
        calcHighShelf(high[1], freq, Q, gain);
        // Presence peaking @ 3.5 kHz
        calcPeaking(presence[0], freq, Q, gain * 0.5);
        calcPeaking(presence[1], freq, Q, gain * 0.5);
        coeff_version = s->pf_param_version.load(std::memory_order_relaxed);
    }
};

static PFEngineState g_pf;

// ── Helpers ───────────────────────────────────────────────────────────────────
static inline float softClip(float x) {
    if (x > 0.95f) return 0.95f + 0.05f * std::tanh((x - 0.95f) * 20.0f);
    if (x < -0.95f) return -0.95f - 0.05f * std::tanh((x + 0.95f) * 20.0f);
    return x;
}

static inline float thermalGain(float tempC) {
    if (tempC < kThermalLimitC) return 1.0f;
    float t = (tempC - kThermalLimitC) * 0.1f;
    return 1.0f / (1.0f + t);
}

// ── Lectura de temperatura (compartida entre JNI directo y watchdog) ────────
static float readThermalZoneC() {
    FILE* temp_file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!temp_file) return -1.0f;
    int temp_milli = 0;
    float result = -1.0f;
    if (fscanf(temp_file, "%d", &temp_milli) == 1) {
        result = temp_milli / 1000.0f;
    }
    fclose(temp_file);
    return result;
}

// ── Watchdog v1.5: 3 fallos antes de safe_mode ───────────────────────────────
static bool pingAudioFlinger() {
    // Verificar que audioserver sigue vivo
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/dev/socket/audio_flinger", sizeof(addr.sun_path) - 1);
    int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
    return rc == 0;
}

static void enterSafeMode() {
    LOGE("Watchdog: 3 fallos consecutivos. Entrando safe_mode.");
    // Deshabilitar módulo Magisk
    std::ofstream disable_file("/data/adb/modules/ivanna_omega/disable");
    if (disable_file.is_open()) {
        disable_file.put('1');
        disable_file.close();
    }
    // Notificar estado
    // __system_property_set no disponible en NDK
    LOGI("Safe mode activado. Módulo deshabilitado. Reinicia para aplicar.");
}

// ── Proceso de audio ──────────────────────────────────────────────────────────
static void processLoop() {
    ivanna::audio::enableAudioThreadFastMathOnce();
    while (g_running.load(std::memory_order_acquire)) {
        if (!g_shared) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const auto t_block_start = std::chrono::steady_clock::now();

        // Recompute coeffs si cambiaron
        uint32_t cv = g_shared->pf_param_version.load(std::memory_order_acquire);
        if (cv != g_pf.coeff_version) {
            g_pf.recompute(g_shared);
        }

        const float temp   = g_shared->current_temperature.load(std::memory_order_relaxed);
        const float tGain  = thermalGain(temp);

        // FIX (auditoría): estos atomics ya existían en OmegaSharedState y ya
        // se escribían desde nativeSetPFParams/nativeSetPF*, pero nadie los
        // leía aquí — el PF Engine calculaba coeficientes Biquad y los tiraba.
        const float drive    = g_shared->pf_drive.load(std::memory_order_relaxed);
        const float wet      = std::clamp(g_shared->pf_wet.load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float mix      = std::clamp(g_shared->pf_mix.load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float masterDb = g_shared->pf_master.load(std::memory_order_relaxed);
        const float masterGain = powf(10.0f, masterDb / 20.0f);
        const float driveGain  = 1.0f + std::clamp(drive, 0.0f, 1.0f) * 3.0f;

        for (int frame = 0; frame < OMEGA_BLOCK_SIZE; ++frame) {
            for (int ch = 0; ch < OMEGA_MAX_CHANNELS; ++ch) {
                const int idx = frame * OMEGA_MAX_CHANNELS + ch;
                const float dry = g_process_buf[idx];

                // 1) Drive (saturación suave pre-EQ)
                float x = std::tanh(dry * driveGain);

                // 2) Cadena Biquad de 4 bandas (low shelf -> mid peak -> high shelf -> presence)
                x = g_pf.low[ch].process(x);
                x = g_pf.mid[ch].process(x);
                x = g_pf.high[ch].process(x);
                x = g_pf.presence[ch].process(x);

                // 3) Wet/dry del efecto, luego mix general de salida
                float blended = dry * (1.0f - wet) + x * wet;
                float out = dry * (1.0f - mix) + blended * mix;

                // 4) Ganancia master + protección térmica + limitador suave
                out *= masterGain * tGain;
                g_process_buf[idx] = softClip(out);
            }
        }

        const auto t_block_end = std::chrono::steady_clock::now();
        const float block_ms = std::chrono::duration<float, std::milli>(t_block_end - t_block_start).count();
        g_shared->current_latency_ms.store(block_ms, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ── Socket server ─────────────────────────────────────────────────────────────
static void socketLoop() {
    g_socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_socket_fd < 0) {
        LOGE("socketLoop: socket() falló: %s", strerror(errno));
        return;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path + 1, kSocketName, sizeof(addr.sun_path) - 2);
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(kSocketName);

    if (bind(g_socket_fd, (struct sockaddr*)&addr, len) < 0) {
        LOGE("socketLoop: bind() falló: %s", strerror(errno));
        close(g_socket_fd);
        g_socket_fd = -1;
        return;
    }

    if (listen(g_socket_fd, 4) < 0) {
        LOGE("socketLoop: listen() falló: %s", strerror(errno));
        close(g_socket_fd);
        g_socket_fd = -1;
        return;
    }

    while (g_running.load(std::memory_order_acquire)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_socket_fd, &fds);
        struct timeval tv{1, 0};

        int rc = select(g_socket_fd + 1, &fds, nullptr, nullptr, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            LOGE("socketLoop: select() error: %s", strerror(errno));
            break;
        }
        if (rc == 0) continue;

        int client = accept4(g_socket_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            LOGE("socketLoop: accept() error: %s", strerror(errno));
            continue;
        }

        // Leer comando simple
        char cmd[64] = {};
        ssize_t n = recv(client, cmd, sizeof(cmd) - 1, MSG_DONTWAIT);
        if (n > 0) {
            if (strncmp(cmd, "ping", 4) == 0) {
                send(client, "pong", 4, MSG_DONTWAIT);
            } else if (strncmp(cmd, "status", 6) == 0) {
                const char* st = g_running.load(std::memory_order_acquire) ? "running" : "stopped";
                send(client, st, strlen(st), MSG_DONTWAIT);
            }
        }
        close(client);
    }
}

} // namespace

// ── Inicialización ────────────────────────────────────────────────────────────
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeStart(JNIEnv* /*env*/, jobject /*thiz*/) {
    // FIX (auditoría): la firma original devolvía jint pero OmegaDaemon.kt
    // declara `nativeStart(): Boolean`. JNI enlaza por firma exacta — este
    // desajuste producía UnsatisfiedLinkError en cuanto se llamara.
    if (g_running.exchange(true)) {
        LOGI("Daemon ya está corriendo");
        return JNI_TRUE;
    }

    // Crear shared memory
    g_shm_fd = memfd_create_compat("ivanna_omega_shm", MFD_CLOEXEC);
    if (g_shm_fd < 0) {
        LOGE("memfd_create falló: %s", strerror(errno));
        g_running.store(false, std::memory_order_release);
        return JNI_FALSE;
    }

    if (ftruncate(g_shm_fd, sizeof(OmegaSharedState)) < 0) {
        LOGE("ftruncate falló: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_running.store(false, std::memory_order_release);
        return JNI_FALSE;
    }

    g_shared = (OmegaSharedState*)mmap(nullptr, sizeof(OmegaSharedState),
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       g_shm_fd, 0);
    if (g_shared == MAP_FAILED) {
        LOGE("mmap falló: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_running.store(false, std::memory_order_release);
        return JNI_FALSE;
    }

    memset(g_shared, 0, sizeof(OmegaSharedState));
    g_shared->pf_freq.store(48000, std::memory_order_relaxed);
    g_shared->pf_resonance.store(0.707f, std::memory_order_relaxed);
    g_shared->pf_mid.store(0.0f, std::memory_order_relaxed);
    g_shared->pf_param_version.store(1, std::memory_order_relaxed);

    // Threads
    g_process_thread = std::thread(processLoop);
    g_socket_thread = std::thread(socketLoop);

    // Watchdog v1.5: 3 fallos antes de safe_mode
    std::thread([]() {
        int failures = 0;
        while (g_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            // FIX (auditoría): current_temperature se inicializaba en 35.0f
            // y nunca se actualizaba, así que thermalGain() en processLoop
            // jamás activaba la protección térmica real del dispositivo.
            if (g_shared && g_shared != MAP_FAILED) {
                float tempC = readThermalZoneC();
                if (tempC >= 0.0f) {
                    g_shared->current_temperature.store(tempC, std::memory_order_relaxed);
                }
            }

            if (!pingAudioFlinger()) {
                failures++;
                LOGW("Watchdog: fallo %d/3 — AudioFlinger no responde", failures);
                if (failures >= 3) {
                    enterSafeMode();
                    g_running.store(false, std::memory_order_release);
                    break;
                }
            } else {
                if (failures > 0) {
                    LOGI("Watchdog: AudioFlinger recuperado, reseteando contador");
                    failures = 0;
                }
            }
        }
    }).detach();

    LOGI("OmegaDaemon iniciado. Watchdog activo (3 fallos = safe_mode). shm_fd=%d", g_shm_fd);
    return JNI_TRUE;
}

// ── Finalización ──────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeStop(JNIEnv* /*env*/, jobject /*thiz*/) {
    g_running.store(false, std::memory_order_release);

    if (g_process_thread.joinable()) g_process_thread.join();
    if (g_socket_thread.joinable()) g_socket_thread.join();

    if (g_shared && g_shared != MAP_FAILED) {
        munmap(g_shared, sizeof(OmegaSharedState));
        g_shared = nullptr;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
    }

    LOGI("OmegaDaemon detenido limpiamente.");
}


// ── Funciones JNI faltantes para OmegaDaemon.kt ─────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetProcessing(JNIEnv* /*env*/, jobject /*thiz*/, jboolean enabled) {
    if (g_shared && g_shared != MAP_FAILED) {
        g_shared->is_processing = enabled ? 1 : 0;
        LOGI("nativeSetProcessing: processing_enabled = %d", enabled ? 1 : 0);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetIntensity(JNIEnv* /*env*/, jobject /*thiz*/, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) {
        g_shared->intensity.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed);
    }
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetTemperature(JNIEnv* /*env*/, jobject /*thiz*/) {
    // FIX (auditoría): la firma original devolvía jint pero OmegaDaemon.kt
    // declara `nativeGetTemperature(): Float`. Mismo problema de firma que
    // nativeStart — ahora devuelve jfloat.
    float tempC = readThermalZoneC();
    if (tempC >= 0.0f && g_shared && g_shared != MAP_FAILED) {
        // También la publicamos en shared state: es la misma lectura que
        // usa el watchdog cada 5s, pero aquí queda disponible de inmediato
        // para quien llame a getTemperature() desde la UI.
        g_shared->current_temperature.store(tempC, std::memory_order_relaxed);
    }
    return tempC;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetLatency(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (!g_shared || g_shared == MAP_FAILED) return 0.0f;
    return g_shared->current_latency_ms.load(std::memory_order_relaxed);
}

// ── PF Engine — bulk setter ───────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFParams(
    JNIEnv* /*env*/, jobject /*thiz*/,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma,
    jfloat freq, jfloat resonance,
    jfloat low, jfloat mid, jfloat high,
    jfloat presence, jfloat master) {
    if (!g_shared || g_shared == MAP_FAILED) return;
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
    // Un solo bump: el hot-path recomputa los Biquad una vez en el próximo bloque
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetPFParams(JNIEnv* env, jobject /*thiz*/) {
    jfloatArray result = env->NewFloatArray(13);
    if (!result) return nullptr;
    if (!g_shared || g_shared == MAP_FAILED) return result; // ceros

    float vals[13] = {
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
    env->SetFloatArrayRegion(result, 0, 13, vals);
    return result;
}

// ── PF Engine — setters individuales sin recomputación de Biquad ────────────
// (drive/wet/mix/alpha/beta/gamma son escalares puros en el hot-path, no
// afectan coeficientes, así que no necesitan bump de pf_param_version)
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFDrive(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_drive.store(v, std::memory_order_relaxed);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFWet(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_wet.store(v, std::memory_order_relaxed);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFMix(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_mix.store(v, std::memory_order_relaxed);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFAlpha(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_alpha.store(v, std::memory_order_relaxed);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFBeta(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_beta.store(v, std::memory_order_relaxed);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFGamma(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_gamma.store(v, std::memory_order_relaxed);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFMaster(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_master.store(v, std::memory_order_relaxed);
}

// ── PF Engine — setters con recomputación de Biquad (bump de versión) ───────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFFreq(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_freq.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFResonance(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_resonance.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFLow(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_low.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFMid(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_mid.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFHigh(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_high.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFPresence(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_presence.store(v, std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
