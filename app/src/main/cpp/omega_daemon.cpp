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
#include <algorithm>
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

// DSP Ruta B — unificación: mismos módulos reales que Ruta A.
// No hay nueva dependencia de símbolo: omega_daemon.cpp compila dentro de
// libivanna_omega.so junto a dsp/Compressor.cpp, dsp/HarmonicExciter.cpp,
// dsp/StereoWidener.cpp y dsp/SafetyLimiter.cpp (ver CMakeLists.txt).
#include "Compressor.h"
#include "HarmonicExciter.h"
#include "StereoWidener.h"
#include "SafetyLimiter.h"
// Fase 6 — Spatial Engine bridge (Ruta B)
// HRTFConvolver es header-only. Sus dependencias (fft_radix2.hpp,
// synthetic_hrtf.hpp, audio_thread_priority.h) ya están en la misma .so.
// El include path "spatial/" está en include_directories del CMakeLists.
#include "spatial/hrtf_convolver.hpp"

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

// FIX (band energy, ver BandEnergyMeter más abajo): calcPeaking con
// gainDB=0 es identidad (no filtra nada) — hace falta un pasabanda RBJ
// real para poder separar low/mid/high de verdad.
static void calcBandpass(Biquad& bq, float freq, float Q) {
    float w0 = 2.0f * M_PI * freq / 48000.0f;
    float alpha = sinf(w0) / (2.0f * Q);
    float cosw0 = cosf(w0);

    float a0 = 1.0f + alpha;
    bq.b0 = alpha / a0;
    bq.b1 = 0.0f;
    bq.b2 = -alpha / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
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

// ── Band energy meter (Ruta B) ────────────────────────────────────────────────
// FIX (cierre de band energy — antes hardcodeado en 0.0f, Adaptive Engine a
// ciegas en esta ruta): 3 filtros de medición DEDICADOS (no confundir con
// g_pf.low/mid/high, que son shelves/peak APLICADOS a la señal de salida).
// Reutilizan Biquad + calcPeaking, ya definidos arriba en este mismo archivo
// para el PF EQ — no se agrega ninguna dependencia nueva. Deliberadamente NO
// se reutiliza BiquadEnvelopeBank (pd_engine.hpp/neuromorphic/) porque esa
// clase llama a phase_oracle_velocity(), símbolo definido en phase_oracle.cpp
// — parte del .so de la app, no de este binario del daemon. Enlazarlo acá
// exigiría arrastrar phase_oracle.cpp (y sus propias dependencias) a un
// proceso de sistema (audioserver) solo para medir energía de banda; el
// mismo tipo de error de enlace ("undefined symbol") que ya se corrigió una
// vez esta sesión (ver commit de g_shared). Se mide sobre la señal SECA
// (antes del softclip/EQ de salida), igual criterio que usa la Ruta A para
// su ensanchamiento adaptativo/Voice Protection.
struct BandEnergyMeter {
    Biquad low[2], mid[2], high[2];
    float envLow = 0.f, envMid = 0.f, envHigh = 0.f;

    void init() {
        Biquad tmpL{}, tmpR{};
        calcBandpass(tmpL,  150.f, 0.9f); low[0]  = tmpL; low[0].reset();
        calcBandpass(tmpR,  150.f, 0.9f); low[1]  = tmpR; low[1].reset();
        calcBandpass(tmpL, 1000.f, 0.9f); mid[0]  = tmpL; mid[0].reset();
        calcBandpass(tmpR, 1000.f, 0.9f); mid[1]  = tmpR; mid[1].reset();
        calcBandpass(tmpL, 6000.f, 0.9f); high[0] = tmpL; high[0].reset();
        calcBandpass(tmpR, 6000.f, 0.9f); high[1] = tmpR; high[1].reset();
    }

    void tick(float l, float r) {
        const float bl = (low[0].process(l)  + low[1].process(r))  * 0.5f;
        const float bm = (mid[0].process(l)  + mid[1].process(r))  * 0.5f;
        const float bh = (high[0].process(l) + high[1].process(r)) * 0.5f;
        const float el = std::fabs(bl), em = std::fabs(bm), eh = std::fabs(bh);
        constexpr float ATK = 0.05f, REL = 0.01f;
        envLow  += ((el > envLow)  ? ATK : REL) * (el - envLow);
        envMid  += ((em > envMid)  ? ATK : REL) * (em - envMid);
        envHigh += ((eh > envHigh) ? ATK : REL) * (eh - envHigh);
    }
};
static BandEnergyMeter g_bandMeter;

// ── DSP Ruta B — módulos unificados ──────────────────────────────────────────
// Instancias estáticas de los mismos módulos que usa Ruta A (DSPBridge).
// Se inicializan con DSPParams default en omega_daemon_start() y se
// actualizan con runtime setters desde processLoop() antes de cada bloque.
// No hay estado compartido con las instancias de Ruta A — cada ruta tiene
// las suyas. La coherencia de comportamiento viene del código, no del estado.
static ivanna::Compressor      g_comp_b;
static ivanna::HarmonicExciter g_exciter_b;
static ivanna::StereoWidener   g_widener_b;
static ivanna::SafetyLimiter   g_limiter_b;
// Fase 6 — Spatial: instancia global del HRTFConvolver (header-only).
// No está en el hot-path hasta que ai_spatial_enabled==true.
// Se inicializa una sola vez en omega_daemon_start(); reset() seguro desde
// cualquier hilo porque no usa malloc después de init().
static ivanna::HRTFConvolver   g_hrtf_b;
static bool                    g_dsp_b_initialized = false;

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
            // target_gain adaptativo — solo atenuación [0.5, 1.0].
            const float adaptiveGain = std::clamp(
                g_shared->ai_runtime_gain_mul.load(std::memory_order_relaxed),
                0.5f, 1.0f);

            // Leer runtime setters de unificación DSP (nuevos campos en
            // omega_shared.h). Cargados una vez por bloque, no por sample.
            const float compAmt  = std::clamp(
                g_shared->ai_runtime_comp_amount.load(std::memory_order_relaxed),
                0.0f, 1.0f);
            const float excRed   = std::clamp(
                g_shared->ai_runtime_exciter_red.load(std::memory_order_relaxed),
                0.0f, 1.0f);
            g_comp_b.setRuntimeAmount(compAmt);
            g_exciter_b.setRuntimeReduction(excRed);

            // Estereo intercalado: [L0,R0, L1,R1, ...]
            for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                float l = work_buf[i * 2];
                float r = work_buf[i * 2 + 1];
                const float origL = l, origR = r;

                // Medir energía de banda sobre la señal SECA (antes del
                // procesamiento) — mismo criterio que Ruta A.
                g_bandMeter.tick(origL, origR);

                // 1. Pre-gain + saturacion tanh
                const float dv = 1.0f + drive * 8.0f;
                l = softClip(l * dv);
                r = softClip(r * dv);

                // 2. 4-band PF EQ
                l = g_pf.low[0].process(l);       r = g_pf.low[1].process(r);
                l = g_pf.mid[0].process(l);       r = g_pf.mid[1].process(r);
                l = g_pf.high[0].process(l);      r = g_pf.high[1].process(r);
                l = g_pf.presence[0].process(l);  r = g_pf.presence[1].process(r);

                // 3. Mezcla wet/dry + limitación térmica + ganancia maestra
                //    + target_gain adaptativo
                work_buf[i * 2]     = (wet * l + dry * origL) * tGain * master * adaptiveGain;
                work_buf[i * 2 + 1] = (wet * r + dry * origR) * tGain * master * adaptiveGain;
            }

            // 4. Compressor — opera sobre el bloque completo (deinterlaced L/R
            //    por la API de ivanna::Compressor). Necesita buffers separados.
            //    Separamos, procesamos, reinterlazamos.
            {
                alignas(16) float tmpL[OMEGA_BLOCK_SIZE], tmpR[OMEGA_BLOCK_SIZE];
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    tmpL[i] = work_buf[i * 2];
                    tmpR[i] = work_buf[i * 2 + 1];
                }
                g_comp_b.process(tmpL, tmpR, OMEGA_BLOCK_SIZE);
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    work_buf[i * 2]     = tmpL[i];
                    work_buf[i * 2 + 1] = tmpR[i];
                }
            }

            // 5. HarmonicExciter
            {
                alignas(16) float tmpL[OMEGA_BLOCK_SIZE], tmpR[OMEGA_BLOCK_SIZE];
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    tmpL[i] = work_buf[i * 2];
                    tmpR[i] = work_buf[i * 2 + 1];
                }
                g_exciter_b.process(tmpL, tmpR, OMEGA_BLOCK_SIZE);
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    work_buf[i * 2]     = tmpL[i];
                    work_buf[i * 2 + 1] = tmpR[i];
                }
            }

            // 6. StereoWidener — ensanche M/S con crossover mono-safe de graves.
            //    width controlado por ai_sensitivity repurposado: NO, no tocamos
            //    campos existentes con significado diferente. El widener corre con
            //    su default (width=1.0f, configurado en init). Se puede exponer
            //    vía socket como SET_PF_ALPHA en una iteración posterior.
            {
                alignas(16) float tmpL[OMEGA_BLOCK_SIZE], tmpR[OMEGA_BLOCK_SIZE];
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    tmpL[i] = work_buf[i * 2];
                    tmpR[i] = work_buf[i * 2 + 1];
                }
                g_widener_b.process(tmpL, tmpR, OMEGA_BLOCK_SIZE);
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    work_buf[i * 2]     = tmpL[i];
                    work_buf[i * 2 + 1] = tmpR[i];
                }
            }

            // 7. Spatial Engine (HRTFConvolver) — Fase 6, opcional vía ai_spatial_enabled.
            //    Posición: después de Widener (señal ya ensanchada) y antes de
            //    SafetyLimiter (protección sigue siendo la última etapa).
            //    Reglas de seguridad:
            //      - Solo activa si ai_spatial_enabled == true (false por defecto).
            //      - set_position() es barato (solo cuantiza y marca crossfade si
            //        cambió el ángulo) — seguro llamarlo en el hot-path cada bloque.
            //      - process() no llama malloc tras init(); usa std::vector interno
            //        pero sus buffers están pre-reservados.
            //      - No toca SafetyLimiter ni Ruta A.
            if (g_shared->ai_spatial_enabled.load(std::memory_order_relaxed)) {
                const float azimuth = g_shared->ai_spatial_azimuth.load(
                    std::memory_order_relaxed);
                const float aggr = std::clamp(
                    g_shared->ai_spatial_aggressiveness.load(std::memory_order_relaxed),
                    0.0f, 1.0f);
                g_hrtf_b.set_position(azimuth, aggr);

                alignas(16) float tmpL[OMEGA_BLOCK_SIZE], tmpR[OMEGA_BLOCK_SIZE];
                alignas(16) float outL[OMEGA_BLOCK_SIZE], outR[OMEGA_BLOCK_SIZE];
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    tmpL[i] = work_buf[i * 2];
                    tmpR[i] = work_buf[i * 2 + 1];
                }
                g_hrtf_b.process(tmpL, tmpR, outL, outR, OMEGA_BLOCK_SIZE);
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    work_buf[i * 2]     = outL[i];
                    work_buf[i * 2 + 1] = outR[i];
                }
            }

            // 8. SafetyLimiter — -0.1 dBFS ceiling, RT-safe, sin malloc.
            //    Mismo rol que en Ruta A: última línea de defensa antes de
            //    entregar a AudioFlinger.
            {
                alignas(16) float tmpL[OMEGA_BLOCK_SIZE], tmpR[OMEGA_BLOCK_SIZE];
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    tmpL[i] = work_buf[i * 2];
                    tmpR[i] = work_buf[i * 2 + 1];
                }
                g_limiter_b.process(tmpL, tmpR, OMEGA_BLOCK_SIZE);
                for (int i = 0; i < OMEGA_BLOCK_SIZE; ++i) {
                    work_buf[i * 2]     = tmpL[i];
                    work_buf[i * 2 + 1] = tmpR[i];
                }
            }

            // Publicar band energy del bloque (una vez por bloque —
            // barato, sin malloc/mutex, solo 3 atomic stores).
            g_shared->ai_band_low.store(g_bandMeter.envLow,   std::memory_order_relaxed);
            g_shared->ai_band_mid.store(g_bandMeter.envMid,   std::memory_order_relaxed);
            g_shared->ai_band_high.store(g_bandMeter.envHigh, std::memory_order_relaxed);
        }

        // ── Push bloque procesado a ring_out ──────────────────────────────────────
        g_shared->ring_out.tryPush(work_buf, blockSamples,
                                    &g_shared->output_buffer[0][0]);
    }
}

// ── Parseo de comandos del socket (OmegaEngineBridge) ────────────────────────
//
// FIX v2.0: el server solo respondía "ping"/"status". OmegaEngineBridge
// envía "SET_PF_LOW:0.5", "SET_PF_MID:-2.0", etc. — comandos que eran
// ignorados silenciosamente. Se añade parseo completo del protocolo.
static void handleSocketCommand(const char* cmd, size_t len) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    // Buscar separador ':'
    const char* colon = (const char*)memchr(cmd, ':', len);
    if (!colon) return;
    const float v = strtof(colon + 1, nullptr);

    // EQ bands — requieren recompute de Biquads (bump version)
    if      (strncmp(cmd, "SET_PF_LOW:",       11) == 0) { g_shared->pf_low.store(v,       std::memory_order_release); g_shared->pf_param_version.fetch_add(1, std::memory_order_release); }
    else if (strncmp(cmd, "SET_PF_MID:",       11) == 0) { g_shared->pf_mid.store(v,       std::memory_order_release); g_shared->pf_param_version.fetch_add(1, std::memory_order_release); }
    else if (strncmp(cmd, "SET_PF_HIGH:",      12) == 0) { g_shared->pf_high.store(v,      std::memory_order_release); g_shared->pf_param_version.fetch_add(1, std::memory_order_release); }
    else if (strncmp(cmd, "SET_PF_PRESENCE:",  16) == 0) { g_shared->pf_presence.store(v,  std::memory_order_release); g_shared->pf_param_version.fetch_add(1, std::memory_order_release); }
    else if (strncmp(cmd, "SET_PF_FREQ:",      12) == 0) { g_shared->pf_freq.store(v,      std::memory_order_release); g_shared->pf_param_version.fetch_add(1, std::memory_order_release); }
    else if (strncmp(cmd, "SET_PF_RESONANCE:", 17) == 0) { g_shared->pf_resonance.store(v, std::memory_order_release); g_shared->pf_param_version.fetch_add(1, std::memory_order_release); }
    // Scalares — sin recompute
    else if (strncmp(cmd, "SET_PF_DRIVE:",   13) == 0)  g_shared->pf_drive.store(v,    std::memory_order_release);
    else if (strncmp(cmd, "SET_PF_WET:",     11) == 0)  g_shared->pf_wet.store(v,      std::memory_order_release);
    else if (strncmp(cmd, "SET_PF_MIX:",     11) == 0)  g_shared->pf_mix.store(v,      std::memory_order_release);
    else if (strncmp(cmd, "SET_PF_ALPHA:",   13) == 0)  g_shared->pf_alpha.store(v,    std::memory_order_release);
    else if (strncmp(cmd, "SET_PF_BETA:",    12) == 0)  g_shared->pf_beta.store(v,     std::memory_order_release);
    else if (strncmp(cmd, "SET_PF_GAMMA:",   13) == 0)  g_shared->pf_gamma.store(v,    std::memory_order_release);
    else if (strncmp(cmd, "SET_PF_MASTER:",  14) == 0)  g_shared->pf_master.store(v,   std::memory_order_release);
    // FIX (comando fantasma): OmegaEngineBridge.setVocoderMix() manda
    // "SET_VOCODER_MIX:x" desde el día uno del socket bridge, pero nunca
    // fue parseado aquí — caía siempre a "comando desconocido". El campo
    // vocoder_mix SÍ existe en OmegaSharedState (constructor lo inicializa
    // en 0.8f), así que sólo faltaba esta línea para que el valor de la UI
    // llegara al shared state. processLoop() aún no tiene una etapa de
    // vocoder en la cadena DSP — eso es trabajo aparte, no de este fix —
    // pero al menos el parámetro ya no se pierde en el camino.
    else if (strncmp(cmd, "SET_VOCODER_MIX:", 16) == 0) g_shared->vocoder_mix.store(v, std::memory_order_release);
    // Control
    else if (strncmp(cmd, "SET_PROCESSING:", 15) == 0)  g_shared->is_processing.store(v != 0.0f, std::memory_order_release);
    else if (strncmp(cmd, "SET_BYPASS:",     11) == 0)  g_shared->bypass_enabled.store(v != 0.0f, std::memory_order_release);
    else if (strncmp(cmd, "SET_INTENSITY:",  14) == 0)  g_shared->intensity.store(v,   std::memory_order_release);
    // AI
    else if (strncmp(cmd, "SET_AI_ENABLED:",     15) == 0) g_shared->ai_enabled.store(v != 0.0f,  std::memory_order_release);
    else if (strncmp(cmd, "SET_AI_AUTO_ADAPT:",  18) == 0) g_shared->ai_auto_adapt.store(v != 0.0f, std::memory_order_release);
    else if (strncmp(cmd, "SET_AI_SENSITIVITY:", 19) == 0) g_shared->ai_sensitivity.store(v, std::memory_order_release);
    else LOGW("handleSocketCommand: comando desconocido: %.*s", (int)(colon - cmd), cmd);
}

// FIX (comando fantasma): OmegaEngineBridge.resetDefaults() manda
// "RESET_DEFAULTS" — SIN ':'. handleSocketCommand() exige un separador ':'
// como primer paso (`memchr(cmd, ':', len)`), así que este comando se
// descartaba ANTES de siquiera loguearse como desconocido; era, en la
// práctica, un botón que no hacía nada. Se despacha aparte, como ping/
// status/GET_TELEMETRY, y reinicia los 13 parámetros del PF Engine a los
// mismos valores que usa el constructor de OmegaSharedState.
static void resetPFDefaults() {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_drive.store(0.65f,     std::memory_order_relaxed);
    g_shared->pf_wet.store(0.5f,        std::memory_order_relaxed);
    g_shared->pf_mix.store(0.7f,        std::memory_order_relaxed);
    g_shared->pf_alpha.store(0.5f,      std::memory_order_relaxed);
    g_shared->pf_beta.store(0.5f,       std::memory_order_relaxed);
    g_shared->pf_gamma.store(0.5f,      std::memory_order_relaxed);
    g_shared->pf_freq.store(1000.0f,    std::memory_order_relaxed);
    g_shared->pf_resonance.store(0.707f, std::memory_order_relaxed);
    g_shared->pf_low.store(0.0f,        std::memory_order_relaxed);
    g_shared->pf_mid.store(0.0f,        std::memory_order_relaxed);
    g_shared->pf_high.store(0.0f,       std::memory_order_relaxed);
    g_shared->pf_presence.store(0.0f,   std::memory_order_relaxed);
    g_shared->pf_master.store(0.0f,     std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
    LOGI("RESET_DEFAULTS: PF Engine restaurado a valores de fábrica");
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

        // Leer comando (non-blocking, 5ms timeout)
        char cmd[128] = {};
        ssize_t n = recv(client, cmd, sizeof(cmd) - 1, MSG_DONTWAIT);

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // FIX v2.0: sin datos inmediatos → omega_effect.cpp esperando
            // el SHM FD vía SCM_RIGHTS (recvmsg bloqueante sin enviar nada).
            // Enviamos el FD por el mecanismo SCM_RIGHTS del kernel.
            if (g_shm_fd >= 0) {
                char zero = '\0';
                struct iovec iov{ &zero, 1 };
                char cm[CMSG_SPACE(sizeof(int))]{};
                struct msghdr msgh{};
                msgh.msg_iov = &iov;  msgh.msg_iovlen = 1;
                msgh.msg_control = cm; msgh.msg_controllen = sizeof(cm);
                struct cmsghdr* hdr = CMSG_FIRSTHDR(&msgh);
                hdr->cmsg_level = SOL_SOCKET;
                hdr->cmsg_type  = SCM_RIGHTS;
                hdr->cmsg_len   = CMSG_LEN(sizeof(int));
                memcpy(CMSG_DATA(hdr), &g_shm_fd, sizeof(int));
                if (sendmsg(client, &msgh, 0) < 0)
                    LOGE("socketLoop: sendmsg SCM_RIGHTS falló: %s", strerror(errno));
                else
                    LOGI("socketLoop: SHM FD=%d enviado via SCM_RIGHTS", g_shm_fd);
            }
        } else if (n > 0) {
            cmd[n] = '\0';
            if (strncmp(cmd, "ping", 4) == 0) {
                send(client, "pong\n", 5, MSG_DONTWAIT);
            } else if (strncmp(cmd, "status", 6) == 0) {
                const char* st = g_running.load() ? "running\n" : "stopped\n";
                send(client, st, strlen(st), MSG_DONTWAIT);
            } else if (strncmp(cmd, "GET_TELEMETRY", 13) == 0) {
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "rms=%.4f,gain_db=%.2f,temp=%d,lat=%.2f\n",
                    g_shared ? g_shared->ai_rms_level.load(std::memory_order_relaxed) : 0.0f,
                    g_shared ? g_shared->ai_gain_db.load(std::memory_order_relaxed) : 0.0f,
                    g_shared ? (int)g_shared->current_temperature.load(std::memory_order_relaxed) : 0,
                    g_shared ? g_shared->current_latency_ms.load(std::memory_order_relaxed) : 0.0f);
                send(client, resp, strlen(resp), MSG_DONTWAIT);
            } else if (strncmp(cmd, "RESET_DEFAULTS", 14) == 0) {
                resetPFDefaults();
                send(client, "ok\n", 3, MSG_DONTWAIT);
            } else {
                handleSocketCommand(cmd, (size_t)n);
            }

            // FIX CRÍTICO (causa real de trabazón/crash al mover sliders):
            // este socket se cerraba con close(client) despues de UN solo
            // comando. OmegaEngineBridge.kt (lado Kotlin) asume conexion
            // persistente y, via DSPState.pushToNative() -> setPFParams(),
            // manda 13 comandos SET_PF_* seguidos por CADA tick de CUALQUIER
            // slider del DSP (onExciterChange/onEqChange/onWidthChange/etc,
            // que disparan en cada frame del gesto de arrastre, decenas de
            // veces por segundo, en el hilo principal de la UI). Con el
            // cierre inmediato, cada uno de esos 13 comandos forzaba al
            // cliente a reconectar (LocalSocket.connect() bloqueante) antes
            // de escribir el siguiente — hasta 13 conexiones TCP-like
            // sincronas en el hilo de UI por cada tick de slider. Eso es
            // ANR/trabazon garantizada, y con carga sostenida (arrastrar el
            // slider) puede degenerar en que el sistema mate el proceso.
            //
            // Fix: en vez de cerrar de inmediato, seguir leyendo del MISMO
            // cliente (no-blocking, con una espera corta entre intentos) por
            // hasta ~150ms de inactividad total, o hasta que el cliente
            // cierre su lado (recv devuelve 0). Esto cubre el caso real de
            // 13 comandos consecutivos sin cambiar el protocolo de traspaso
            // SHM via SCM_RIGHTS (rama de arriba, que sigue cerrando
            // inmediatamente porque ahi "sin datos" YA es la señal completa
            // para ese cliente — omega_effect.cpp no manda comandos de texto).
            const int kIdleBudgetMs = 150;
            int idleMs = 0;
            while (idleMs < kIdleBudgetMs) {
                char cmd2[128] = {};
                ssize_t n2 = recv(client, cmd2, sizeof(cmd2) - 1, MSG_DONTWAIT);
                if (n2 == 0) {
                    break; // cliente cerró su lado — fin de la sesión
                } else if (n2 < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        idleMs += 5;
                        continue;
                    }
                    break; // error real de socket
                }
                cmd2[n2] = '\0';
                idleMs = 0; // llegó dato — resetear el presupuesto de inactividad
                if (strncmp(cmd2, "ping", 4) == 0) {
                    send(client, "pong\n", 5, MSG_DONTWAIT);
                } else if (strncmp(cmd2, "status", 6) == 0) {
                    const char* st = g_running.load() ? "running\n" : "stopped\n";
                    send(client, st, strlen(st), MSG_DONTWAIT);
                } else if (strncmp(cmd2, "GET_TELEMETRY", 13) == 0) {
                    char resp[128];
                    snprintf(resp, sizeof(resp),
                        "rms=%.4f,gain_db=%.2f,temp=%d,lat=%.2f\n",
                        g_shared ? g_shared->ai_rms_level.load(std::memory_order_relaxed) : 0.0f,
                        g_shared ? g_shared->ai_gain_db.load(std::memory_order_relaxed) : 0.0f,
                        g_shared ? (int)g_shared->current_temperature.load(std::memory_order_relaxed) : 0,
                        g_shared ? g_shared->current_latency_ms.load(std::memory_order_relaxed) : 0.0f);
                    send(client, resp, strlen(resp), MSG_DONTWAIT);
                } else if (strncmp(cmd2, "RESET_DEFAULTS", 14) == 0) {
                    resetPFDefaults();
                    send(client, "ok\n", 3, MSG_DONTWAIT);
                } else {
                    handleSocketCommand(cmd2, (size_t)n2);
                }
            }
        }
        close(client);
    }
}

} // namespace

// ── Inicialización ────────────────────────────────────────────────────────────
//
// FIX (BUG 6 parcial): declaraba jint pero Kotlin espera `external fun
// nativeStart(): Boolean`. jint/jboolean comparten registro de retorno en
// ARM así que no crasheaba, pero el valor cruzaba el límite JNI sin la
// semántica correcta (el fd real nunca se necesitó del lado Kotlin — se usa
// internamente vía g_shm_fd). Ahora retorna jboolean explícito.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeStart(JNIEnv* /*env*/, jobject /*thiz*/) {
    if (g_running.exchange(true)) {
        LOGI("Daemon ya está corriendo");
        return JNI_TRUE;
    }

    // Crear shared memory
    g_shm_fd = memfd_create_compat("ivanna_omega_shm", MFD_CLOEXEC);
    if (g_shm_fd < 0) {
        LOGE("memfd_create falló: %s", strerror(errno));
        g_running = false;
        return JNI_FALSE;
    }

    if (ftruncate(g_shm_fd, sizeof(OmegaSharedState)) < 0) {
        LOGE("ftruncate falló: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_running = false;
        return JNI_FALSE;
    }

    g_shared = (OmegaSharedState*)mmap(nullptr, sizeof(OmegaSharedState),
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       g_shm_fd, 0);
    if (g_shared == MAP_FAILED) {
        LOGE("mmap falló: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_running = false;
        return JNI_FALSE;
    }

    memset(g_shared, 0, sizeof(OmegaSharedState));
    g_shared->pf_freq.store(48000, std::memory_order_relaxed);
    g_shared->pf_resonance.store(0.707f, std::memory_order_relaxed);
    g_shared->pf_mid.store(0.0f, std::memory_order_relaxed);
    g_shared->pf_param_version.store(1, std::memory_order_relaxed);
    // FIX CRÍTICO (band energy + Opción A de unificación): g_shared vive en
    // memoria mmap() cruda — el constructor de OmegaSharedState (con sus
    // defaults como ai_runtime_gain_mul(1.0f)) NUNCA se ejecuta acá, el
    // memset(0) de arriba deja estos campos en 0.0 real. Sin este fix,
    // ai_runtime_gain_mul en 0.0 multiplicaría el audio de streaming por
    // cero — silencio total — hasta que el puente de la app escribiera un
    // valor real, lo cual no está garantizado en un arranque en silencio.
    g_shared->ai_runtime_gain_mul.store(1.0f, std::memory_order_relaxed);
    // Misma lógica que ai_runtime_gain_mul: el constructor nunca corre en mmap,
    // así que los nuevos campos también quedan en 0.0f por el memset.
    // 0.0f es seguro para ambos (comp_amount=0 → sin compresión extra,
    // exciter_red=0 → sin reducción), así que no requieren store explícito.
    // Se documenta aquí por consistencia, no por necesidad.
    // g_shared->ai_runtime_comp_amount = 0.0f  ← seguro, memset ya lo hizo
    // g_shared->ai_runtime_exciter_red = 0.0f  ← idem
    g_bandMeter.init();

    // Inicializar módulos DSP Ruta B con params por defecto.
    // setParams() aplica DSPParams defaults (calibrados en dsp_types.h v3.3).
    if (!g_dsp_b_initialized) {
        ivanna::DSPParams p{};
        g_comp_b.setParams(p);
        g_exciter_b.setParams(p);
        g_widener_b.setParams(p);
        g_limiter_b.setParams();   // SafetyLimiter usa su propia firma sin DSPParams
        // Fase 6 — inicializar HRTFConvolver con sample rate real.
        // init() asigna los std::vector internos (único malloc aceptable:
        // ocurre antes del audio thread, no en el hot-path).
        g_hrtf_b.init(OMEGA_SAMPLE_RATE);
        g_dsp_b_initialized = true;
        LOGI("DSP Ruta B inicializado (Compressor/Exciter/Widener/Limiter/HRTFConvolver).");
    }

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
    return JNI_TRUE;
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

// FIX (BUG 6 — el más grave de la auditoría): declaraba jint pero Kotlin
// espera `external fun nativeGetTemperature(): Float`. A diferencia de
// nativeStart (jint/jboolean, mismo registro entero en ARM), int y float
// usan registros de retorno DISTINTOS en la ABI (r0/x0 vs s0/d0) — el valor
// entero nunca llegaba al registro que Kotlin lee, así que el valor recibido
// era literalmente basura del registro flotante sin inicializar, no una
// reinterpretación válida del entero. Ahora retorna jfloat real.
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetTemperature(JNIEnv* /*env*/, jobject /*thiz*/) {
    FILE* temp_file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (temp_file) {
        int temp_milli = 0;
        if (fscanf(temp_file, "%d", &temp_milli) == 1) {
            fclose(temp_file);
            return (jfloat)temp_milli / 1000.0f;  // °C con precisión decimal
        }
        fclose(temp_file);
    }
    // Fallback: última temperatura conocida del hot-path del daemon
    // (current_temperature ya la mantiene actualizada el watchdog/efecto).
    if (g_shared && g_shared != MAP_FAILED) {
        return g_shared->current_temperature.load(std::memory_order_relaxed);
    }
    return -1.0f;  // sin sysfs y sin daemon corriendo
}

// ── FIX (BUG 7): 16 funciones declaradas `external fun` en OmegaDaemon.kt
// sin implementación nativa → UnsatisfiedLinkError garantizado en la
// primera llamada a cualquiera de ellas (setIntensity, getLatency, todo el
// bulk/individual setter del PF Engine). Se implementan todas contra
// OmegaSharedState — mismo patrón defensivo (g_shared nullptr/MAP_FAILED
// guard) que ya usa nativeSetProcessing.
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetIntensity(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->intensity.store(v, std::memory_order_release);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetLatency(JNIEnv*, jobject) {
    if (g_shared && g_shared != MAP_FAILED)
        return g_shared->current_latency_ms.load(std::memory_order_relaxed);
    return 0.0f;
}

// Bulk setter — un solo bump de pf_param_version para los 13 parámetros
// (coincide con el doc de OmegaDaemon.kt: "un solo bump de coeff_version").
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma,
    jfloat freq, jfloat resonance,
    jfloat low, jfloat mid, jfloat high,
    jfloat presence, jfloat master) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_drive.store(drive,       std::memory_order_relaxed);
    g_shared->pf_wet.store(wet,           std::memory_order_relaxed);
    g_shared->pf_mix.store(mix,           std::memory_order_relaxed);
    g_shared->pf_alpha.store(alpha,       std::memory_order_relaxed);
    g_shared->pf_beta.store(beta,         std::memory_order_relaxed);
    g_shared->pf_gamma.store(gamma,       std::memory_order_relaxed);
    g_shared->pf_freq.store(freq,         std::memory_order_relaxed);
    g_shared->pf_resonance.store(resonance, std::memory_order_relaxed);
    g_shared->pf_low.store(low,           std::memory_order_relaxed);
    g_shared->pf_mid.store(mid,           std::memory_order_relaxed);
    g_shared->pf_high.store(high,         std::memory_order_relaxed);
    g_shared->pf_presence.store(presence, std::memory_order_relaxed);
    g_shared->pf_master.store(master,     std::memory_order_relaxed);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeGetPFParams(JNIEnv* env, jobject) {
    jfloat out[13] = {0};
    if (g_shared && g_shared != MAP_FAILED) {
        out[0]  = g_shared->pf_drive.load(std::memory_order_relaxed);
        out[1]  = g_shared->pf_wet.load(std::memory_order_relaxed);
        out[2]  = g_shared->pf_mix.load(std::memory_order_relaxed);
        out[3]  = g_shared->pf_alpha.load(std::memory_order_relaxed);
        out[4]  = g_shared->pf_beta.load(std::memory_order_relaxed);
        out[5]  = g_shared->pf_gamma.load(std::memory_order_relaxed);
        out[6]  = g_shared->pf_freq.load(std::memory_order_relaxed);
        out[7]  = g_shared->pf_resonance.load(std::memory_order_relaxed);
        out[8]  = g_shared->pf_low.load(std::memory_order_relaxed);
        out[9]  = g_shared->pf_mid.load(std::memory_order_relaxed);
        out[10] = g_shared->pf_high.load(std::memory_order_relaxed);
        out[11] = g_shared->pf_presence.load(std::memory_order_relaxed);
        out[12] = g_shared->pf_master.load(std::memory_order_relaxed);
    }
    jfloatArray arr = env->NewFloatArray(13);
    if (arr != nullptr) env->SetFloatArrayRegion(arr, 0, 13, out);
    return arr;
}

// Setters individuales SIN recomputación de Biquad (escalares del hot-path,
// coincide con el comentario de OmegaDaemon.kt).
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFDrive(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_drive.store(v, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFWet(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_wet.store(v, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFMix(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_mix.store(v, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFAlpha(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_alpha.store(v, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFBeta(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_beta.store(v, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFGamma(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_gamma.store(v, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFMaster(JNIEnv*, jobject, jfloat v) {
    if (g_shared && g_shared != MAP_FAILED) g_shared->pf_master.store(v, std::memory_order_release);
}

// Setters individuales CON recomputación de Biquad (bump de pf_param_version) —
// coincide con el comentario de OmegaDaemon.kt para freq/resonance/low/mid/
// high/presence.
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFFreq(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_freq.store(v, std::memory_order_release);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFResonance(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_resonance.store(v, std::memory_order_release);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFLow(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_low.store(v, std::memory_order_release);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFMid(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_mid.store(v, std::memory_order_release);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFHigh(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_high.store(v, std::memory_order_release);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaDaemon_nativeSetPFPresence(JNIEnv*, jobject, jfloat v) {
    if (!g_shared || g_shared == MAP_FAILED) return;
    g_shared->pf_presence.store(v, std::memory_order_release);
    g_shared->pf_param_version.fetch_add(1, std::memory_order_release);
}

// FIX (build roto — ld: undefined symbol: g_shared): g_shared está
// declarado DENTRO del namespace anónimo de este archivo (líneas 53-514,
// `namespace { ... OmegaSharedState* g_shared = nullptr; ... }`), lo que
// le da enlace INTERNO (equivalente a 'static' a nivel de archivo) — sólo
// visible dentro de este mismo translation unit. Un `extern
// OmegaSharedState* g_shared;` desde otro .cpp (ivanna_omega_jni.cpp,
// mismo target `ivanna_omega`, pero OTRA unidad de compilación) nunca
// podía enlazar contra él, aunque ambos terminen en el mismo .so — de ahí
// el "undefined symbol" en el linker, no un problema de CMake ni de
// targets.
//
// Esta función SÍ tiene enlace externo real (está fuera del namespace
// anónimo), y su cuerpo puede leer g_shared sin calificar porque está en
// la MISMA unidad de compilación que la declaración original — la regla
// de resolución de nombres de C++ hace visible un namespace anónimo en
// todo el resto del archivo que lo contiene, sólo restringe la
// LINKAGE (visibilidad desde otros .cpp), no el lookup dentro del mismo.
OmegaSharedState* omega_daemon_get_shared_state() {
    return g_shared;
}
