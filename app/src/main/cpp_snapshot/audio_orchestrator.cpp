#include <jni.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <array>
#include <fstream>
#include <mutex>           // ← faltaba en el original
#include <android/log.h>
#include "omega_shared.h"
#include "evolutionary_kernel.h"

#define LOG_TAG "AudioOrchestrator"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Estado global ─────────────────────────────────────────────────────────────

struct OrchestratorState {
    float dialogGain  = 0.f;
    float bassGain    = 0.f;
    float widenerWet  = 0.f;
    bool  manifoldEnabled = false;
    float anti_dolby_speech = 0.f;
    float anti_dolby_music  = 0.f;
    float anti_dolby_bass   = 0.f;
    // AUDIT FIX (símbolos JNI huérfanos): AudioEngine.kt declara
    // nativeSetGain / nativeSetEqGain / nativeSetWidth como external fun y
    // las llama en runtime (setGain, setEqGain, setWidth) — sin símbolo C++
    // ni campo de estado, el runCatching de Kotlin tragaba el
    // UnsatisfiedLinkError y el slider quedaba silenciosamente inoperante.
    float masterGainDb = 0.f;   // slider "Gain" / "Master Gain" (dB)
    float eqGainDb     = 0.f;   // slider "EQ Gain" (dB, se suma al master)
    float stereoWidth  = 0.f;   // slider "Width" [0..1] — se suma a widenerWet
    // Métricas para nativeGetLufs / nativeGetPeakDbfs (se actualizan en
    // ivanna_orchestrate tras el cálculo de rms/peak ya existente).
    float lastLufs     = -70.f;
    float lastPeakDbfs = -70.f;
    float loudness_curve[256]{};
    struct KalmanScalar { float q, r, x, p; };
    KalmanScalar kalman_loud  {0.001f, 0.1f,  0.0f, 1.0f};
    KalmanScalar kalman_trans {0.005f, 0.2f,  0.0f, 1.0f};
    uint8_t active_genome[GENOME_SIZE]{};
    bool     genome_valid      = false;
    uint32_t genome_generation = 0;
};

static OrchestratorState g_orch;
static std::mutex         g_orch_mutex;

// ── Helpers internos ──────────────────────────────────────────────────────────

static void update_anti_dolby(float speech, float music, float bass) {
    g_orch.anti_dolby_speech = std::max(0.f, std::min(1.f, speech));
    g_orch.anti_dolby_music  = std::max(0.f, std::min(1.f, music));
    g_orch.anti_dolby_bass   = std::max(0.f, std::min(1.f, bass));
}

static void init_loudness_curve() {
    static const float freqs[] = {
        20,25,31.5f,40,50,63,80,100,125,160,200,250,315,400,500,630,
        800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000,10000,12500
    };
    static const float levels[] = {
        0,-0.5f,-1.6f,-3.2f,-4.1f,-4.8f,-5.4f,-5.7f,-5.3f,-4.5f,
        -3.4f,-2.2f,-1.0f,0.3f,1.3f,1.8f,1.9f,1.8f,1.5f,1.0f,
        0.5f,-0.2f,-1.5f,-3.0f,-4.8f,-6.5f,-8.2f,-9.7f,-10.5f
    };
    static constexpr int kN = static_cast<int>(sizeof(freqs)/sizeof(freqs[0]));
    for (int i = 0; i < 256; ++i) {
        float freq = 20.f * std::pow(2.f, i * (std::log2(20000.f/20.f)/255.f));
        int idx = 0;
        while (idx < kN-1 && freqs[idx] < freq) ++idx;
        if (idx == 0) {
            g_orch.loudness_curve[i] = levels[0];
        } else {
            float t = (freq - freqs[idx-1]) / (freqs[idx] - freqs[idx-1]);
            g_orch.loudness_curve[i] = levels[idx-1] + t*(levels[idx]-levels[idx-1]);
        }
    }
}

static void kalman_update(float* x, float* p, float q, float r, float m) {
    *p += q;
    float k = *p / (*p + r);
    *x += k * (m - *x);
    *p  = (1.f - k) * (*p);
}

// ── API pública (C linkage) ───────────────────────────────────────────────────

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScores(
    JNIEnv*, jobject, jfloat speech, jfloat music, jfloat bass)
{
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    update_anti_dolby(speech, music, bass);
}

extern "C" void ivanna_set_anti_dolby_scores(float speech, float music, float bass) {
    if (!std::isfinite(speech) || !std::isfinite(music) || !std::isfinite(bass)) return;
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    update_anti_dolby(speech, music, bass);
}

extern "C" void ivanna_set_route_profile(float bassBoostDb,
                                          float dialogBoostDb,
                                          float widenerMult) {
    if (!std::isfinite(bassBoostDb) ||
        !std::isfinite(dialogBoostDb) ||
        !std::isfinite(widenerMult)) return;
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    g_orch.bassGain   = bassBoostDb;
    g_orch.dialogGain = dialogBoostDb;
    g_orch.widenerWet = widenerMult;
}

extern "C" void ivanna_set_manifold_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    g_orch.manifoldEnabled = enabled;
}

// AUDIT FIX (símbolos JNI huérfanos): setters/getters para los sliders de
// AudioEngine.kt que no tenían destino en el estado del orchestrator.
extern "C" void ivanna_set_master_gain(float db) {
    if (!std::isfinite(db)) return;
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    g_orch.masterGainDb = std::clamp(db, -24.f, 24.f);
}

extern "C" void ivanna_set_eq_gain(float db) {
    if (!std::isfinite(db)) return;
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    g_orch.eqGainDb = std::clamp(db, -12.f, 12.f);
}

extern "C" void ivanna_set_stereo_width(float width) {
    if (!std::isfinite(width)) return;
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    g_orch.stereoWidth = std::clamp(width, 0.f, 1.f);
}

extern "C" float ivanna_get_lufs() {
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    return g_orch.lastLufs;
}

extern "C" float ivanna_get_peak_dbfs() {
    std::lock_guard<std::mutex> lock(g_orch_mutex);
    return g_orch.lastPeakDbfs;
}

extern "C" void ivanna_orchestrate(float* buffer, int samples,
                                    int channels, int /*sampleRate*/) {
    std::lock_guard<std::mutex> lock(g_orch_mutex);

    // Métricas de nivel
    float rms = 0.f, peak = 0.f;
    for (int i = 0; i < samples; ++i) {
        float s = std::fabs(buffer[i]);
        if (s > peak) peak = s;
        rms += buffer[i] * buffer[i];
    }
    rms = std::sqrt(rms / samples);

    kalman_update(&g_orch.kalman_loud.x,  &g_orch.kalman_loud.p,
                  0.001f, 0.1f, 20.f*std::log10(rms+1e-9f));
    kalman_update(&g_orch.kalman_trans.x, &g_orch.kalman_trans.p,
                  0.005f, 0.2f, peak/(rms+1e-9f));

    // AUDIT FIX: persistir métricas para nativeGetLufs / nativeGetPeakDbfs.
    // Aproximación deliberada: LUFS ≈ RMS en dBFS (sin K-weighting); es la
    // misma escala que el UI ya mostraba con los getters huérfanos.
    g_orch.lastLufs     = 20.f * std::log10(rms  + 1e-9f);
    g_orch.lastPeakDbfs = 20.f * std::log10(peak + 1e-9f);

    // Ganancias (aplicadas a todos los canales uniformemente)
    // AUDIT FIX: masterGainDb + eqGainDb (sliders UI) se suman al bus de
    // ganancia existente — sin cambiar el comportamiento de dialog/bass.
    float dialogLin = std::pow(10.f, g_orch.dialogGain / 20.f);
    float bassLin   = std::pow(10.f, g_orch.bassGain   / 20.f);
    float masterLin = std::pow(10.f, (g_orch.masterGainDb + g_orch.eqGainDb) / 20.f);
    float combined  = dialogLin * bassLin * masterLin;
    for (int i = 0; i < samples; ++i) buffer[i] *= combined;

    // Stereo widener (M/S)
    // AUDIT FIX: stereoWidth (slider UI [0..1]) se suma a widenerWet (route
    // profile) — el canal de la app y el del perfil conviven sin pisarse.
    const float wetTotal = g_orch.widenerWet + g_orch.stereoWidth;
    if (channels == 2 && wetTotal > 0.01f) {
        float wet = wetTotal;
        for (int i = 0; i < samples; i += 2) {
            float mid  = (buffer[i] + buffer[i+1]) * 0.5f;
            float side = (buffer[i] - buffer[i+1]) * 0.5f * (1.f + wet);
            buffer[i]   = mid + side;
            buffer[i+1] = mid - side;
        }
    }

    // Genome manifold
    if (g_orch.manifoldEnabled && evo_best_fitness() > 0.5f) {
        uint8_t gen[GENOME_SIZE];
        evo_get_best_genome(gen, GENOME_SIZE);
        for (int i = 0; i < GENOME_SIZE; ++i)
            g_orch.active_genome[i] =
                static_cast<uint8_t>((g_orch.active_genome[i]*3 + gen[i]) / 4);
        g_orch.genome_valid      = true;
        g_orch.genome_generation = evo_get_generation();
    }
    if (g_orch.genome_valid) {
        for (int i = 0; i < samples; ++i) {
            int idx = (i * 256) / samples;
            float env = g_orch.active_genome[idx] / 255.f;
            buffer[i] += buffer[i] * env * 0.1f;
        }
    }
}

// ── Inicialización del módulo ─────────────────────────────────────────────────

__attribute__((constructor))
static void init_orchestrator() {
    init_loudness_curve();
}
