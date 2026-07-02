/*
 * ivanna_omega_jni.cpp — IVANNA OMEGA SUPREME
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Arquitectura OPE post-refactor (control-frame):
 *   DSP chain → PDEngine (NHO + BiquadEnvelopeBank + CueBasedSpatial)
 *   EvolutionaryKernel movido a modo OFFLINE (no corre en audio thread)
 *
 * Refactor de desacoplo JNI <-> pipeline de audio:
 *   - El hilo JNI (llamado desde el hilo de UI/control de Android) YA NO
 *     toca g_eq/g_comp/g_exciter/g_widener/g_gain/g_pd directamente.
 *     Cada setter solo construye un ControlFrame nuevo y lo publica en
 *     g_control_bus (ver control_frame.hpp).
 *   - El hilo de audio (nativeProcess / nativeProcessBlock) consume el
 *     frame más reciente UNA sola vez, al principio de cada bloque, y lo
 *     aplica de forma congelada antes de procesar. Dentro del bloque los
 *     parámetros no cambian: process_block() es una función pura de
 *     (entrada, frame congelado, estado interno) → salida = determinismo
 *     por bloque.
 *   - No quedan setters directos del DSP alcanzables desde fuera de este
 *     archivo: PDEngine expone applyControlFrame() como único punto de
 *     entrada externo (los set_* individuales son privados).
 */

#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <cmath>
#include <atomic>
#include <algorithm>

#include "../include/dsp_types.h"
#include "../include/ParametricEQ.h"
#include "../include/Compressor.h"
#include "../include/HarmonicExciter.h"
#include "../include/StereoWidener.h"
#include "../include/GainStage.h"
#include "../pd_engine.hpp"
#include "../control_frame.hpp"

#define LOG_TAG "IVANNA-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace ivanna;

// ── Engine singletons (static storage — zero allocations) ────────────────────
// Estos objetos SOLO son tocados por el hilo de audio a partir de este
// refactor. El hilo JNI nunca vuelve a llamarles setParams()/set_*().
static ParametricEQ    g_eq;
static Compressor      g_comp;
static HarmonicExciter g_exciter;
static StereoWidener   g_widener;
static GainStage       g_gain;
static PDEngine        g_pd;    // NHO + BiquadEnvelopeBank + CueBasedSpatial
static uint32_t        g_sample_rate{48000};
static std::atomic<bool> g_initialized{false};

// ── Bus de control lock-free (seqlock) — único canal JNI → audio thread ──
// NOTE: se definen dentro de namespace ivanna { … } (no-static + fuera del
// scope local) para que audio_control_plane.cpp pueda referenciarlos como
// 'ivanna::g_control_bus' / 'ivanna::g_staging_frame'. Un 'using namespace'
// NO introduce nombres definidos DESPUÉS en el namespace de destino; por
// eso hace falta el bloque explícito.
namespace ivanna {
    ControlFrameBus g_control_bus;

    // Copia de staging en el hilo de control (JNI/UI). No es tocada por el
    // hilo de audio; cada setter la actualiza y publica una copia inmutable.
    ControlFrame    g_staging_frame;
}

// Última secuencia aplicada por CADA camino de audio (DSPBridge vs
// IvannaNativeLib comparten motor pero pueden ser llamados por hilos de
// audio distintos según la ruta de la app, así que cada uno lleva su
// propio "último visto" para no perder una actualización).
static thread_local uint64_t t_last_seq_dspbridge{0};
static thread_local uint64_t t_last_seq_nativelib{0};

// ── Helper ───────────────────────────────────────────────────────────────────
static inline bool copyJFloat(JNIEnv* env, jfloatArray src, float* dst, int n) {
    if (!src || n <= 0) return false;
    jfloat* p = env->GetFloatArrayElements(src, nullptr);
    if (!p) return false;
    memcpy(dst, p, n * sizeof(float));
    env->ReleaseFloatArrayElements(src, p, JNI_ABORT);
    return true;
}

// Aplica, si hay un frame más nuevo, el ControlFrame congelado a TODA la
// cadena (EQ/Comp/Exciter/Widener/Gain + PDEngine) en un solo lugar.
// Llamado SOLO desde el hilo de audio, como máximo una vez por bloque,
// antes de tocar una sola muestra. Esto es lo que hace que un bloque sea
// determinista: durante su procesamiento, ningún parámetro puede cambiar.
static inline void apply_pending_control_frame(uint64_t& lastSeenSeq) noexcept {
    ControlFrame f;
    if (!g_control_bus.consumeIfNewer(f, lastSeenSeq)) return;  // nada nuevo: no recalcular

    const DSPParams p = f.toDSPParams(g_sample_rate);
    g_eq.setParams(p);
    g_comp.setParams(p);
    g_exciter.setParams(p);
    g_widener.setParams(p);
    g_gain.setParams(p);
    g_pd.applyControlFrame(f);
}

extern "C" {

// ═══════════════════════════════════════════════════════════════════════════════
// DSPBridge (com.ivanna.omega.dsp.DSPBridge) — called at app startup
// ═══════════════════════════════════════════════════════════════════════════════

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("IVANNA OMEGA SUPREME v1.9-OPE (control-frame) | GORE TNS © 2026");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeInit(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 192000) { LOGE("Bad SR: %d", sr); return; }
    g_sample_rate = (uint32_t)sr;

    // Estado inicial explícito: se publica un ControlFrame por defecto y
    // se aplica de inmediato (en este mismo hilo, antes de que exista
    // hilo de audio corriendo) para que el primer bloque procesado ya
    // tenga coeficientes válidos — nunca hay un bloque "sin configurar".
    g_staging_frame = ControlFrame{};
    g_control_bus.publish(g_staging_frame);

    g_pd.init((uint32_t)sr);
    uint64_t seq0 = 0;
    apply_pending_control_frame(seq0);
    t_last_seq_dspbridge = seq0;
    t_last_seq_nativelib = seq0;

    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("OPE initialized @ %d Hz (EvolutionaryKernel online, control-frame decoupled)", sr);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma_v,
    jfloat freq,  jfloat resonance,
    jfloat low,   jfloat mid,  jfloat high,
    jfloat presence, jfloat master) {
    // Solo construye y publica un ControlFrame — no toca ningún objeto DSP.
    g_staging_frame.drive = drive; g_staging_frame.wet = wet; g_staging_frame.mix = mix;
    g_staging_frame.alpha = alpha; g_staging_frame.beta = beta; g_staging_frame.gamma_v = gamma_v;
    g_staging_frame.freq  = freq;  g_staging_frame.resonance = resonance;
    g_staging_frame.low   = low;   g_staging_frame.mid = mid;   g_staging_frame.high = high;
    g_staging_frame.presence = presence; g_staging_frame.master = master;
    // NHO parameters mapped from DSP params (igual que antes, pero vía frame)
    g_staging_frame.nho_alpha = alpha;
    g_staging_frame.nho_beta  = beta;
    g_staging_frame.nho_wet   = wet * 0.5f;
    g_control_bus.publish(g_staging_frame);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess(
    JNIEnv* env, jobject, jfloatArray buf, jint nFrames) {
    if (!g_initialized.load(std::memory_order_acquire)) return;
    if (!buf || nFrames <= 0) return;

    // Sincronización de control: una sola vez por bloque, antes de procesar
    // ninguna muestra. A partir de aquí el bloque entero es determinista.
    apply_pending_control_frame(t_last_seq_dspbridge);

    const int n = std::min((int)nFrames, 2048);
    jfloat* data = env->GetFloatArrayElements(buf, nullptr);
    if (!data) return;

    // FIX v1.7 — CABLEADO OPE: el buffer llega intercalado estéreo
    // (L,R,L,R...) desde AudioPipeline.
    static thread_local float inL[2048], inR[2048];
    static thread_local float outL[2048], outR[2048];

    for (int i = 0; i < n; ++i) {
        inL[i] = data[2 * i];
        inR[i] = data[2 * i + 1];
    }

    // Cadena DSP real por canal
    g_eq.process(inL, inR, n);
    g_comp.process(inL, inR, n);
    g_exciter.process(inL, inR, n);
    g_widener.process(inL, inR, n);
    g_gain.processInput(inL, inR, n);

    // PDEngine: modo 0 = passthrough, 1 = +NHO, 2 = +NHO+Spatial.
    g_pd.process_block(inL, inR, outL, outR, n);

    g_gain.processOutput(outL, outR, n);

    for (int i = 0; i < n; ++i) {
        data[2 * i]     = std::clamp(outL[i], -1.0f, 1.0f);
        data[2 * i + 1] = std::clamp(outR[i], -1.0f, 1.0f);
    }

    env->ReleaseFloatArrayElements(buf, data, 0);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReset(JNIEnv*, jobject) {
    g_pd.stop_evo_thread();
    g_eq.reset(); g_comp.reset(); g_exciter.reset();
    g_widener.reset(); g_gain.reset(); g_pd.reset();
    LOGI("OPE reset");
}

// ═══════════════════════════════════════════════════════════════════════════════
// IvannaNativeLib (com.ivanna.omega.core.IvannaNativeLib) — stereo block API
// ═══════════════════════════════════════════════════════════════════════════════

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitDSP(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 192000) return JNI_FALSE;
    g_sample_rate = (uint32_t)sr;

    g_staging_frame = ControlFrame{};
    g_control_bus.publish(g_staging_frame);

    g_pd.init((uint32_t)sr);
    uint64_t seq0 = 0;
    apply_pending_control_frame(seq0);
    t_last_seq_dspbridge = seq0;
    t_last_seq_nativelib = seq0;

    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("IvannaNativeLib DSP @ %d Hz (EvolutionaryKernel online, control-frame decoupled)", sr);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeProcessBlock(
    JNIEnv* env, jobject,
    jfloatArray inL, jfloatArray inR,
    jfloatArray outL, jfloatArray outR,
    jint frames) {
    if (!g_initialized.load(std::memory_order_acquire) || frames <= 0) return;

    apply_pending_control_frame(t_last_seq_nativelib);

    // Stack buffers — zero allocations
    float lBuf[2048], rBuf[2048], oL[2048], oR[2048];
    const int n = std::min((int)frames, 2048);

    if (!copyJFloat(env, inL, lBuf, n)) return;
    if (!copyJFloat(env, inR, rBuf, n)) return;

    // DSP chain
    g_gain.processInput(lBuf, rBuf, n);
    g_eq.process(lBuf, rBuf, n);
    g_comp.process(lBuf, rBuf, n);
    g_exciter.process(lBuf, rBuf, n);
    g_widener.process(lBuf, rBuf, n);
    g_gain.processOutput(lBuf, rBuf, n);

    // PDEngine (NHO + Spatial on modes 1/2)
    g_pd.process_block(lBuf, rBuf, oL, oR, n);

    jfloat* pL = env->GetFloatArrayElements(outL, nullptr);
    jfloat* pR = env->GetFloatArrayElements(outR, nullptr);
    if (pL) { memcpy(pL, oL, n*sizeof(float)); env->ReleaseFloatArrayElements(outL, pL, 0); }
    if (pR) { memcpy(pR, oR, n*sizeof(float)); env->ReleaseFloatArrayElements(outR, pR, 0); }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetParams(
    JNIEnv* env, jobject, jfloatArray params) {
    if (!params) return;
    jfloat* p = env->GetFloatArrayElements(params, nullptr);
    if (!p) return;
    const int n = env->GetArrayLength(params);
    if (n>=1)  g_staging_frame.drive     = p[0];
    if (n>=2)  g_staging_frame.wet       = p[1];
    if (n>=3)  g_staging_frame.mix       = p[2];
    if (n>=4)  g_staging_frame.alpha     = p[3];
    if (n>=5)  g_staging_frame.beta      = p[4];
    if (n>=6)  g_staging_frame.gamma_v   = p[5];
    if (n>=7)  g_staging_frame.freq      = p[6];
    if (n>=8)  g_staging_frame.resonance = p[7];
    if (n>=9)  g_staging_frame.low       = p[8];
    if (n>=10) g_staging_frame.mid       = p[9];
    if (n>=11) g_staging_frame.high      = p[10];
    if (n>=12) g_staging_frame.presence  = p[11];
    if (n>=13) g_staging_frame.master    = p[12];
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);
    // Publica un frame nuevo — el hilo de audio lo aplicará en su próximo bloque.
    g_control_bus.publish(g_staging_frame);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeResetDSP(JNIEnv*, jobject) {
    g_pd.stop_evo_thread();
    g_eq.reset(); g_comp.reset(); g_exciter.reset();
    g_widener.reset(); g_gain.reset(); g_pd.reset();
}

// PDEngine / NHO setters expuestos a Kotlin — YA NO tocan g_pd. Solo
// actualizan el frame de staging y publican; el hilo de audio aplica.
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAlpha(JNIEnv*,jobject,jfloat v) {
    g_staging_frame.nho_alpha = v; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBeta(JNIEnv*,jobject,jfloat v) {
    g_staging_frame.nho_beta = v; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetGamma(JNIEnv*,jobject,jfloat v) {
    g_staging_frame.spatial_angle_deg = v * 90.f; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetDelta(JNIEnv*,jobject,jfloat v) {
    g_staging_frame.spatial_width = v; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEta(JNIEnv*,jobject,jfloat v) {
    g_staging_frame.nho_wet = v; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHarmonicGain(JNIEnv*,jobject,jfloat v) {
    g_staging_frame.nho_harmonic_gain = v; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHRTFEnabled(JNIEnv*,jobject,jboolean en) {
    g_staging_frame.mode = en ? 2 : 0; g_control_bus.publish(g_staging_frame);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptEnabled(JNIEnv*,jobject,jboolean) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNPMax(JNIEnv*,jobject,jfloat) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionGain(JNIEnv*,jobject,jint,jfloat) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionDelay(JNIEnv*,jobject,jint,jfloat) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitPILSTM(JNIEnv*,jobject) { g_pd.reset(); }

// ═══════════════════════════════════════════════════════════════════════════════
// OmegaEngine mode control
// ═══════════════════════════════════════════════════════════════════════════════

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeSetMode(JNIEnv*, jobject, jint mode) {
    g_staging_frame.mode = std::clamp((int)mode, 0, 2);
    g_control_bus.publish(g_staging_frame);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeGetMode(JNIEnv*, jobject) {
    // Lectura de solo-consulta del modo realmente aplicado por el hilo de
    // audio (no del staging, que puede tener cambios aún no consumidos).
    return (jint)g_pd.get_mode();
}

// ─── EvolutionaryKernel JNI controls ─────────────────────────────────────────

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeStartEvoThread(JNIEnv*, jobject) {
    g_pd.start_evo_thread();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeStopEvoThread(JNIEnv*, jobject) {
    g_pd.stop_evo_thread();
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetEvoBestFitness(JNIEnv*, jobject) {
    return evo_best_fitness();
}

} // extern "C"
