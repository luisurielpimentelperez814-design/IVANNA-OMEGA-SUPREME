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

    // FIX v2.0: usaba pf_mid para las 4 bandas e ignoraba pf_low/pf_high/pf_presence.
    // Ademas usaba pf_freq como frecuencia de low shelf y high shelf (incorrecto:
    // esas bandas son shelves fijos; solo el mid peak sigue pf_freq).
    void recompute(const OmegaSharedState* s) {
        const float Q        = s->pf_resonance.load(std::memory_order_relaxed);
        const float freq     = s->pf_freq.load(std::memory_order_relaxed);     // mid peak, user-adjustable
        const float gainLow  = s->pf_low.load(std::memory_order_relaxed);      // dB — was: pf_mid (BUG)
        const float gainMid  = s->pf_mid.load(std::memory_order_relaxed);      // dB
        const float gainHigh = s->pf_high.load(std::memory_order_relaxed);     // dB — was: pf_mid (BUG)
        const float gainPres = s->pf_presence.load(std::memory_order_relaxed); // dB — was: pf_mid*0.5 (BUG)

        calcLowShelf (low[0],       200.0f, Q, gainLow);   // Bass warmth shelf @ 200 Hz
        calcLowShelf (low[1],       200.0f, Q, gainLow);
        calcPeaking  (mid[0],       freq,   Q, gainMid);   // Mid peak @ pf_freq (default 1kHz)
        calcPeaking  (mid[1],       freq,   Q, gainMid);
        calcHighShelf(high[0],     8000.0f, Q, gainHigh);  // Clarity/air shelf @ 8 kHz
        calcHighShelf(high[1],     8000.0f, Q, gainHigh);
        calcPeaking  (presence[0], 3500.0f, Q, gainPres);  // Definition peak @ 3.5 kHz
        calcPeaking  (presence[1], 3500.0f, Q, gainPres);

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
//
// FIX v2.0 — CONECTAR ring_in/ring_out + APLICAR Biquads
//
// Bugs anteriores:
//   1. Operaba sobre g_process_buf (buffer estatico aislado que nadie llenaba
//      con audio real y cuyo resultado nadie leia — audio muerto).
//   2. Los Biquads del PF Engine se recomputaban correctamente pero nunca se
//      llamaba a .process() — todo el EQ era letra muerta.
//
// Flujo correcto (omega_effect.cpp empuja/jala):
//   omega_effect  →  ring_in (input_buffer)  →  processLoop()
//   processLoop() →  ring_out (output_buffer) →  omega_effect
//
static void processLoop() {
    alignas(64) float work_buf[OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS];
    const int blockSamples = OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS;

    while (g_running.load(std::memory_order_acquire)) {
        if (!g_shared) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // ── Recompute EQ coefficients si version cambi\u00f3 ──────────────────────────
        const uint32_t cv = g_shared->pf_param_version.load(std::memory_order_acquire);
        if (cv != g_pf.coeff_version) {
            g_pf.recompute(g_shared);
        }

        // ── Pop un bloque de ring_in ──────────────────────────────────────────────
        if (!g_shared->ring_in.tryPop(work_buf, blockSamples,
                                       &g_shared->input_buffer[0][0])) {
            // Ring vacio: yield 500us en vez de quemar CPU
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }

        // ── Cadena DSP (si no bypass) ─────────────────────────────────────────────
        const bool bypass = g_shared->bypass_enabled.load(std::memory_order_relaxed);
        if (!bypass && g_shared->is_processing.load(std::memory_order_relaxed)) {
            const float tGain  = thermalGain(
                g_shared->current_temperature.load(std::memory_order_relaxed));
            const float drive  = g_shared->pf_drive.load(std::memory_order_relaxed);
            const float wet    = g_shared->pf_wet.load(std::memory_order_relaxed);
            const float dry    = 1.0f - wet;
            const float master = std::pow(10.0f,
                g_shared->pf_master.load(std::memory_order_relaxed) / 20.0f);

            // Estereo intercalado: [L0,R0, L1,R1, ...]
            for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                float l = work_buf[i * 2];
                float r = work_buf[i * 2 + 1];
                const float origL = l, origR = r;

                // 1. Pre-gain + saturacion tanh
                const float dv = 1.0f + drive * 8.0f;
                l = softClip(l * dv);
                r = softClip(r * dv);

                // 2. 4-band PF EQ — Biquads APLICADOS por primera vez
                l = g_pf.low[0].process(l);       r = g_pf.low[1].process(r);
                l = g_pf.mid[0].process(l);       r = g_pf.mid[1].process(r);
                l = g_pf.high[0].process(l);      r = g_pf.high[1].process(r);
                l = g_pf.presence[0].process(l);  r = g_pf.presence[1].process(r);

                // 3. Mezcla wet/dry + limitacion termica + ganancia maestra
                work_buf[i * 2]     = (wet * l + dry * origL) * tGain * master;
                work_buf[i * 2 + 1] = (wet * r + dry * origR) * tGain * master;
            }
        }

        // ── Push bloque procesado a ring_out ──────────────────────────────────────
        g_shared->ring_out.tryPush(work_buf, blockSamples,
                                    &g_shared->output_buffer[0][0]);
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

    while (g_running.load()) {
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
                const char* st = g_running.load() ? "running" : "stopped";
                send(client, st, strlen(st), MSG_DONTWAIT);
            }
        }
        close(client);
    }
}

} // namespace

// ── Inicialización ────────────────────────────────────────────────────────────
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeStart(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_running.exchange(true)) {
        LOGI("Daemon ya está corriendo");
        return 0;
    }

    // Crear shared memory
    g_shm_fd = memfd_create_compat("ivanna_omega_shm", MFD_CLOEXEC);
    if (g_shm_fd < 0) {
        LOGE("memfd_create falló: %s", strerror(errno));
        g_running = false;
        return -1;
    }

    if (ftruncate(g_shm_fd, sizeof(OmegaSharedState)) < 0) {
        LOGE("ftruncate falló: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_running = false;
        return -1;
    }

    g_shared = (OmegaSharedState*)mmap(nullptr, sizeof(OmegaSharedState),
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       g_shm_fd, 0);
    if (g_shared == MAP_FAILED) {
        LOGE("mmap falló: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_running = false;
        return -1;
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
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!pingAudioFlinger()) {
                failures++;
                LOGW("Watchdog: fallo %d/3 — AudioFlinger no responde", failures);
                if (failures >= 3) {
                    enterSafeMode();
                    g_running = false;
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

    LOGI("OmegaDaemon iniciado. Watchdog activo (3 fallos = safe_mode).");
    return g_shm_fd;
}

// ── Finalización ──────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeStop(JNIEnv* /*env*/, jobject /*thiz*/) {
    g_running = false;

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

extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetTemperature(JNIEnv* /*env*/, jobject /*thiz*/) {
    // Leer temperatura desde /sys/class/thermal/thermal_zone0/temp
    FILE* temp_file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (temp_file) {
        int temp_milli = 0;
        if (fscanf(temp_file, "%d", &temp_milli) == 1) {
            fclose(temp_file);
            return temp_milli / 1000;  // Convertir a grados Celsius
        }
        fclose(temp_file);
    }
    return -1;  // Error o no disponible
}
