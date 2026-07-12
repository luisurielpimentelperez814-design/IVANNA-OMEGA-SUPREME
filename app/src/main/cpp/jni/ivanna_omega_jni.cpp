/*
 * ivanna_omega_jni.cpp — IVANNA OMEGA SUPREME
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Arquitectura OPE post-refactor:
 *   DSP chain → PDEngine (NHO + BiquadEnvelopeBank + CueBasedSpatial)
 *   EvolutionaryKernel movido a modo OFFLINE (no corre en audio thread)
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
#include "../include/SafetyLimiter.h"
#include "../pd_engine.hpp"
#include "../control_frame.hpp"
#include "../audio_control_plane.hpp"
#include "../perceptual_loudness.hpp"

#define LOG_TAG "IVANNA-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace ivanna;

// ── Bus + staging frame (ver audio_control_plane.cpp) ────────────────────────
// Definición real de los externs declarados en audio_control_plane.cpp.
// El hilo JNI/UI publica ControlFrame nuevos aquí; el hilo de audio los
// consume vía ControlFrameBus::consumeIfNewer().
namespace ivanna {
    ControlFrameBus g_control_bus;
    ControlFrame    g_staging_frame;
}

// ── Engine singletons (static storage — zero allocations) ────────────────────
static ParametricEQ   g_eq;
static Compressor     g_comp;
static HarmonicExciter g_exciter;
static StereoWidener  g_widener;
static GainStage      g_gain;
static SafetyLimiter  g_safety_limiter;
static PDEngine       g_pd;    // NHO + BiquadEnvelopeBank + CueBasedSpatial
static DSPParams      g_params;
static std::atomic<bool> g_initialized{false};
// FEATURE (Voice Protection): 0..1, cuánta voz detecta YamnetClassifier en
// el bloque actual. Protege la inteligibilidad de la voz mezclando de
// vuelta hacia la señal seca (pre-DSP) cuando hay voz dominante, en vez de
// dejar que Exciter/Compresor la sobre-procesen.
static std::atomic<float> g_voice_protect_score{0.f};
// FEATURE (Perceptual Optimizer): medidor de loudness K-weighted real
// (ITU-R BS.1770) + trim automático hacia un target. Reemplaza el
// placeholder muerto de audio_control_plane.hpp (output_lufs nunca se
// escribía). Un solo lector/escritor (el hilo de audio), sin necesidad
// de atomic para el objeto en sí.
static ivanna::LoudnessMeter g_loudnessMeter;
static std::atomic<bool> g_loudnessMeterInit{false};
// Target por defecto: -14 LUFS, el estándar de facto de streaming
// (Spotify/YouTube Music) — volumen percibido consistente entre archivos
// sin importar el mastering original.
static std::atomic<float> g_loudness_target{-14.f};

// ── Helper ───────────────────────────────────────────────────────────────────
static inline bool copyJFloat(JNIEnv* env, jfloatArray src, float* dst, int n) {
    if (!src || n <= 0) return false;
    jfloat* p = env->GetFloatArrayElements(src, nullptr);
    if (!p) return false;
    memcpy(dst, p, n * sizeof(float));
    env->ReleaseFloatArrayElements(src, p, JNI_ABORT);
    return true;
}

extern "C" {

// ═══════════════════════════════════════════════════════════════════════════════
// DSPBridge (com.ivanna.omega.dsp.DSPBridge) — called at app startup
// ═══════════════════════════════════════════════════════════════════════════════

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("IVANNA OMEGA SUPREME v1.1-OPE | GORE TNS © 2026");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeInit(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 192000) { LOGE("Bad SR: %d", sr); return; }
    g_params.sampleRate = (uint32_t)sr;
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    g_loudnessMeter.init((float)sr);
    g_loudnessMeterInit.store(true, std::memory_order_release);
    g_initialized.store(true, std::memory_order_release);
    LOGI("OPE initialized @ %d Hz (EvolutionaryKernel online)", sr);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma_v,
    jfloat freq,  jfloat resonance,
    jfloat low,   jfloat mid,  jfloat high,
    jfloat presence, jfloat master) {
    g_params.drive = drive; g_params.wet = wet;   g_params.mix = mix;
    g_params.alpha = alpha; g_params.beta = beta; g_params.gamma = gamma_v;
    g_params.freq  = freq;  g_params.resonance = resonance;
    g_params.low   = low;   g_params.mid = mid;   g_params.high = high;
    g_params.presence = presence; g_params.master = master;
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    // NHO parameters mapped from DSP params
    g_pd.set_nho_alpha(alpha);
    g_pd.set_nho_beta(beta);
    g_pd.set_nho_wet(wet * 0.5f);
}

// FIX (tuning magistral): DSPState.stereoWidth (Kotlin) nunca llegaba al
// motor nativo — pushToNative() no lo incluía en nativeSetParams(), y
// StereoWidener derivaba el ancho de "gamma" (colisión con el timing del
// compresor). Ahora hay un canal dedicado, independiente de setParams().
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetStereoWidth(JNIEnv*, jobject, jfloat width) {
    g_widener.setWidth(width);
}

// FEATURE (Voice Protection): recibe el score de voz (0..1) desde
// VoiceProtectionController (Kotlin, YamnetClassifier real). Canal
// dedicado, no pasa por setParams() — igual patrón que nativeSetStereoWidth
// de arriba, por la misma razón (evitar colisión con otros parámetros).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetVoiceProtectScore(JNIEnv*, jobject, jfloat score) {
    g_voice_protect_score.store(
        std::clamp(score, 0.f, 1.f), std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess(
    JNIEnv* env, jobject, jfloatArray buf, jint nFrames) {
    if (!g_initialized.load(std::memory_order_acquire)) return;
    if (!buf || nFrames <= 0) return;
    const int n = std::min((int)nFrames, 2048);
    jfloat* data = env->GetFloatArrayElements(buf, nullptr);
    if (!data) return;

    // FIX CRÍTICO: 'data' viene INTERCALADO estéreo [L0,R0,L1,R1,...].
    // El código anterior pasaba el mismo puntero como left y right → mono aliasado.
    // Fix: de-intercalar a buffers L/R reales (thread_local: sin stack overhead),
    // correr la cadena en estéreo verdadero, y re-intercalar al final.
    static thread_local float chL[2048], chR[2048];
    for (int i = 0; i < n; ++i) {
        chL[i] = data[2 * i];
        chR[i] = data[2 * i + 1];
    }

    // FEATURE (Voice Protection): copia seca (pre-DSP) para poder mezclar
    // de vuelta hacia ella si YamnetClassifier detecta voz dominante — ver
    // blend al final de esta función, después de PDEngine.
    static thread_local float dryL[2048], dryR[2048];
    std::memcpy(dryL, chL, n * sizeof(float));
    std::memcpy(dryR, chR, n * sizeof(float));

    // FIX (Fase C, pulido de oído absoluto): processInput() (trim de
    // entrada, derivado de p.mix, ±6dB) corría DESPUÉS de EQ/Compressor/
    // Exciter/Widener — violando el orden de una cadena de ganancia
    // estándar (Input Trim → procesadores dependientes del nivel →
    // Output Gain). Con el trim aplicado al final, cada etapa
    // level-dependent (el threshold del compresor, el drive del exciter
    // — ver el clamp de softClip recién corregido) veía el nivel CRUDO,
    // no el nivel que p.mix estaba pensado para normalizar antes de
    // llegar ahí. processOutput() (ganancia final, derivada de p.master)
    // sí pertenece al final de la cadena — ahí se queda.
    g_gain.processInput(chL, chR, n);
    g_eq.process(chL, chR, n);
    g_comp.process(chL, chR, n);
    g_exciter.process(chL, chR, n);
    g_widener.process(chL, chR, n);
    g_gain.processOutput(chL, chR, n);
    g_safety_limiter.process(chL, chR, n);

    // FIX CRÍTICO: este es el único proceso que el bucle de audio real
    // (AudioPipeline.kt → DSPBridge.process()) invoca en cada bloque.
    // PDEngine (NHO + Spatial + HRTF) se inicializa y arranca su hilo
    // evolutivo desde nativeInit(), pero nunca se llamaba aquí — el motor
    // espacial completo y la modulación del Kernel Evolutivo (ver
    // pd_engine.hpp) eran inertes en el audio que realmente suena.
    // Modo 0 (default) hace passthrough exacto dentro de process_block(),
    // así que esto no cambia nada para quien no activó NHO/Spatial/Omega
    // Mode — solo enciende lo que ya estaba construido y esperando.
    if (g_control_frame.evolutionary_active.load(std::memory_order_relaxed)) {
        const float nho_a = 0.5f + g_control_frame.evo_genome_nho[0].load(std::memory_order_relaxed) * 0.4f;
        const float nho_b = 0.1f + g_control_frame.evo_genome_nho[1].load(std::memory_order_relaxed) * 0.3f;
        const float nho_h = std::clamp(g_control_frame.evo_genome_nho[3].load(std::memory_order_relaxed), 0.f, 2.f);
        const float sp_angle = std::clamp(g_control_frame.evo_genome_spatial[0].load(std::memory_order_relaxed) * 120.f, 0.f, 120.f);
        const float sp_width = std::clamp(g_control_frame.evo_genome_spatial[1].load(std::memory_order_relaxed) * 1.5f, 0.f, 1.5f);
        g_pd.set_nho_alpha(nho_a);
        g_pd.set_nho_beta(nho_b);
        g_pd.set_nho_harmonic(nho_h);
        g_pd.set_spatial_angle(sp_angle);
        g_pd.set_spatial_width(sp_width);
    }
    static thread_local float pdOutL[2048], pdOutR[2048];
    g_pd.process_block(chL, chR, pdOutL, pdOutR, n);

    // FEATURE (Spatial adaptativo, fase 1 de "HRTF adaptativo"): mide la
    // correlación L/R real del material SECO (dryL/dryR, antes de
    // cualquier DSP) y aplica un ensanchamiento M/S extra cuando el
    // contenido es casi-mono, sin tocar mezclas que ya vienen anchas por
    // sí solas. Es una etapa INDEPENDIENTE del ángulo/width de
    // CueBasedSpatial (que ya lo controla el Kernel Evolutivo/slider
    // manual) — no compite por el mismo parámetro, evita el mismo tipo de
    // colisión que ya se encontró y corrigió con el compresor.
    // No confundir con el motor binaural de 32 objetos
    // (SpatialAudioEngineV2/HRTFConvolver): ese sigue siendo, a propósito,
    // puro análisis/telemetría (ver su propio comentario de clase sobre el
    // bug de eco que resolvió) — activarlo como salida de audio real es un
    // trabajo aparte, más grande, pendiente.
    {
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        for (int i = 0; i < n; ++i) {
            sumLR += (double)dryL[i] * dryR[i];
            sumLL += (double)dryL[i] * dryL[i];
            sumRR += (double)dryR[i] * dryR[i];
        }
        const double denom = std::sqrt(sumLL * sumRR) + 1e-9;
        const float corrRaw = (float)std::clamp(sumLR / denom, -1.0, 1.0);

        static thread_local float corrSmooth = 0.7f;  // arranca neutral, no en 1.0
        corrSmooth += 0.08f * (corrRaw - corrSmooth);  // EMA suave, sin saltos por transitorio

        // corr alto (≈mono) → ensancha hasta +40%. corr bajo (ya ancho) →
        // no toca (multiplicador 1.0). Zona muerta entre 0.4 y 0.8 para no
        // reaccionar a fluctuaciones normales de una mezcla ya balanceada.
        const float widenAmount = std::clamp((corrSmooth - 0.8f) / 0.2f, 0.f, 1.f) * 0.4f;
        if (widenAmount > 0.005f) {
            const float sideMul = 1.f + widenAmount;
            for (int i = 0; i < n; ++i) {
                const float mid  = (pdOutL[i] + pdOutR[i]) * 0.5f;
                const float side = (pdOutL[i] - pdOutR[i]) * 0.5f * sideMul;
                pdOutL[i] = mid + side;
                pdOutR[i] = mid - side;
            }
        }
    }

    // FEATURE (Voice Protection): cuando YamnetClassifier detecta voz
    // dominante en el bloque, mezcla de vuelta hacia la señal seca en vez
    // de dejar que Exciter/Compresor/Widener sobre-procesen la voz. Máximo
    // 55% de mezcla seca aun con score=1.0 — protege sin anular el DSP.
    const float vp = g_voice_protect_score.load(std::memory_order_relaxed);
    if (vp > 0.01f) {
        constexpr float VOICE_PROTECT_MAX = 0.55f;
        const float dryMix = vp * VOICE_PROTECT_MAX;
        const float wetMix = 1.f - dryMix;
        for (int i = 0; i < n; ++i) {
            pdOutL[i] = pdOutL[i] * wetMix + dryL[i] * dryMix;
            pdOutR[i] = pdOutR[i] * wetMix + dryR[i] * dryMix;
        }
    }

    // FEATURE (Perceptual Optimizer): mide LUFS real (K-weighted) sobre la
    // salida final ya procesada y aplica un trim de ganancia lento hacia
    // el target (-14 LUFS por defecto) — normalización de volumen
    // percibido real, no el placeholder muerto que había antes.
    if (g_loudnessMeterInit.load(std::memory_order_relaxed)) {
        const float lufs = g_loudnessMeter.measure_block(pdOutL, pdOutR, n);
        const float target = g_loudness_target.load(std::memory_order_relaxed);
        const float trim = g_loudnessMeter.update_trim(target);
        g_control_frame.output_lufs.store(lufs, std::memory_order_relaxed);
        if (std::fabs(trim) > 0.01f) {
            const float trimLin = std::pow(10.f, trim / 20.f);
            for (int i = 0; i < n; ++i) {
                pdOutL[i] *= trimLin;
                pdOutR[i] *= trimLin;
            }
        }
    }

    // Re-intercalar el resultado estéreo real de vuelta en `data` — sin downmix.
    for (int i = 0; i < n; ++i) {
        data[2 * i]     = pdOutL[i];
        data[2 * i + 1] = pdOutR[i];
    }

    env->ReleaseFloatArrayElements(buf, data, 0);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReset(JNIEnv*, jobject) {
    g_pd.stop_evo_thread();
    g_eq.reset(); g_comp.reset(); g_exciter.reset();
    g_widener.reset(); g_gain.reset();
    g_safety_limiter.reset(); g_pd.reset();
    LOGI("OPE reset");
}

// ═══════════════════════════════════════════════════════════════════════════════
// IvannaNativeLib (com.ivanna.omega.core.IvannaNativeLib) — stereo block API
// ═══════════════════════════════════════════════════════════════════════════════

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitDSP(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 192000) return JNI_FALSE;
    g_params.sampleRate = (uint32_t)sr;
    g_eq.setParams(g_params); g_comp.setParams(g_params);
    g_exciter.setParams(g_params); g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    g_initialized.store(true, std::memory_order_release);
    LOGI("IvannaNativeLib DSP @ %d Hz (EvolutionaryKernel online)", sr);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeProcessBlock(
    JNIEnv* env, jobject,
    jfloatArray inL, jfloatArray inR,
    jfloatArray outL, jfloatArray outR,
    jint frames) {
    if (!g_initialized.load(std::memory_order_acquire) || frames <= 0) return;

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

    // FIX: Kernel Evolutivo → orquestador central real (antes: el genoma
    // ganador solo llegaba a z[]/harmonic_gain vía apply_evo_genome() interno
    // de PDEngine; evolutionary_active nunca se activaba y
    // control_set_evo_genome() no tenía llamador — el resto del genoma
    // (NHO alpha/beta, Spatial angle/width) se evolucionaba en el vacío).
    // Cuando el Kernel Evolutivo está ON, modula NHO+Spatial en tiempo real
    // con el mejor genoma de la generación actual (mismos rangos que
    // audio_control_plane.cpp). No toca EQ/Comp/Exciter/Widener: esos
    // permanecen bajo control manual/YAMNet hasta que se audite esa fusión.
    if (g_control_frame.evolutionary_active.load(std::memory_order_relaxed)) {
        const float nho_a = 0.5f + g_control_frame.evo_genome_nho[0].load(std::memory_order_relaxed) * 0.4f;
        const float nho_b = 0.1f + g_control_frame.evo_genome_nho[1].load(std::memory_order_relaxed) * 0.3f;
        const float nho_h = std::clamp(g_control_frame.evo_genome_nho[3].load(std::memory_order_relaxed), 0.f, 2.f);
        const float sp_angle = std::clamp(g_control_frame.evo_genome_spatial[0].load(std::memory_order_relaxed) * 120.f, 0.f, 120.f);
        const float sp_width = std::clamp(g_control_frame.evo_genome_spatial[1].load(std::memory_order_relaxed) * 1.5f, 0.f, 1.5f);
        g_pd.set_nho_alpha(nho_a);
        g_pd.set_nho_beta(nho_b);
        g_pd.set_nho_harmonic(nho_h);
        g_pd.set_spatial_angle(sp_angle);
        g_pd.set_spatial_width(sp_width);
    }

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
    if (n>=1)  g_params.drive     = p[0];
    if (n>=2)  g_params.wet       = p[1];
    if (n>=3)  g_params.mix       = p[2];
    if (n>=4)  g_params.alpha     = p[3];
    if (n>=5)  g_params.beta      = p[4];
    if (n>=6)  g_params.gamma     = p[5];
    if (n>=7)  g_params.freq      = p[6];
    if (n>=8)  g_params.resonance = p[7];
    if (n>=9)  g_params.low       = p[8];
    if (n>=10) g_params.mid       = p[9];
    if (n>=11) g_params.high      = p[10];
    if (n>=12) g_params.presence  = p[11];
    if (n>=13) g_params.master    = p[12];
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);
    g_eq.setParams(g_params); g_comp.setParams(g_params);
    g_exciter.setParams(g_params); g_widener.setParams(g_params);
    g_gain.setParams(g_params);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeResetDSP(JNIEnv*, jobject) {
    g_pd.stop_evo_thread();
    g_eq.reset(); g_comp.reset(); g_exciter.reset();
    g_widener.reset();
    g_gain.reset();
    g_safety_limiter.reset();
    g_pd.reset();
}

// PDEngine / NHO setters exposed to Kotlin
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAlpha(JNIEnv*,jobject,jfloat v) { g_pd.set_nho_alpha(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBeta(JNIEnv*,jobject,jfloat v)  { g_pd.set_nho_beta(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetGamma(JNIEnv*,jobject,jfloat v) { g_pd.set_spatial_angle(v * 90.f); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetDelta(JNIEnv*,jobject,jfloat v) { g_pd.set_spatial_width(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEta(JNIEnv*,jobject,jfloat v)   { g_pd.set_nho_wet(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHarmonicGain(JNIEnv*,jobject,jfloat v) { g_pd.set_nho_harmonic(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHRTFEnabled(JNIEnv*,jobject,jboolean en) { g_pd.set_mode(en ? 2 : 0); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptEnabled(JNIEnv*,jobject,jboolean) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNPMax(JNIEnv*,jobject,jfloat) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionGain(JNIEnv*,jobject,jint,jfloat) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionDelay(JNIEnv*,jobject,jint,jfloat) {}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitPILSTM(JNIEnv*,jobject) { g_pd.reset(); }

// ── FIX: cableado UI v3.0 → Compresor y Motor Espacial (parámetros que la
// UI ya exponía por callback pero que no tenían contraparte JNI dedicada) ──

// Compresor (GlassCard "COMPRESOR"): threshold en dB [-24..0], ratio [1..20]:1
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCompressorParams(
    JNIEnv*, jobject, jfloat thresholdDb, jfloat ratio) {
    g_comp.setThreshold(thresholdDb);
    g_comp.setRatio(ratio);
}

// NHO/Espacial (GlassCard "NHO / ESPACIAL"): ángulo en radianes, ancho directo.
// Se declaran explícitas (no reusar nativeSetGamma/nativeSetDelta, que ya
// tienen semántica normalizada [0..1]→grados heredada de la UI PI-LSTM v1).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialAngleRad(
    JNIEnv*, jobject, jfloat rad) {
    g_pd.set_spatial_angle(rad * 57.29578f); // rad → deg
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialWidthDirect(
    JNIEnv*, jobject, jfloat width) {
    g_pd.set_spatial_width(width);
}

// ═══════════════════════════════════════════════════════════════════════════════
// OmegaEngine mode control
// ═══════════════════════════════════════════════════════════════════════════════

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeSetMode(JNIEnv*, jobject, jint mode) {
    g_pd.set_mode(mode);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeGetMode(JNIEnv*, jobject) {
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

// ─── EvolutionaryKernel: persistencia (save/load population) ────────────────
// IMPORTANTE: nativeSetEvoSavePath debe llamarse ANTES de nativeInitDSP/
// DSPBridge.nativeInit, porque start_evo_thread() dispara
// evo_initialize_population() -> intenta cargar el save-state en ese momento.

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEvoSavePath(
    JNIEnv* env, jobject, jstring path) {
    if (!path) { evo_set_save_path(nullptr); return; }
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    if (cpath) {
        evo_set_save_path(cpath);
        env->ReleaseStringUTFChars(path, cpath);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSaveEvoState(JNIEnv*, jobject) {
    return evo_save_state() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLoadEvoState(JNIEnv*, jobject) {
    return evo_load_state() ? JNI_TRUE : JNI_FALSE;
}

// ─── FASE 2: puente JVM ↔ C++ para leer sesgo aprendido ──────────────────
// Cache de JavaVM + method ID de LearningBias.jniGetBiasForActiveContext(String)F.
// audio_control_plane.cpp los usa para consultar el sesgo cada vez que
// control_apply_frame() corre (hilo de control, no audio).
JavaVM*   g_jvm = nullptr;
jclass    g_learningBias_cls = nullptr;
jmethodID g_learningBias_getBias = nullptr;

static void cache_learning_bindings(JNIEnv* env) {
    if (g_learningBias_cls && g_learningBias_getBias) return;
    if (g_jvm == nullptr) env->GetJavaVM(&g_jvm);
    jclass local = env->FindClass("com/ivanna/omega/ai/LearningBias");
    if (!local) { env->ExceptionClear(); return; }
    g_learningBias_cls = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    g_learningBias_getBias = env->GetStaticMethodID(
        g_learningBias_cls, "jniGetBiasForActiveContext", "(Ljava/lang/String;)F");
    if (!g_learningBias_getBias) env->ExceptionClear();
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeApplyControlFrame(JNIEnv* env, jobject) {
    cache_learning_bindings(env);
    return (jint) control_apply_frame();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetLearningContext(
    JNIEnv* env, jobject, jstring ctx) {
    cache_learning_bindings(env);
    if (!ctx || !g_learningBias_cls) return;
    jmethodID mid = env->GetStaticMethodID(
        g_learningBias_cls, "jniSetActiveContext", "(Ljava/lang/String;)V");
    if (!mid) { env->ExceptionClear(); return; }
    env->CallStaticVoidMethod(g_learningBias_cls, mid, ctx);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// Consulta el sesgo aprendido para un paramKey. Devuelve 0 si no hay JVM
// o el método no está cacheado. Llamada desde audio_control_plane.cpp.
extern "C" float learning_bias_get(const char* param_key) {
    if (!g_jvm || !g_learningBias_cls || !g_learningBias_getBias || !param_key) return 0.f;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        // Android NDK: AttachCurrentThread(JNIEnv**, void*); OpenJDK: (void**, void*).
        // El cast a void** funciona en ambos (Android acepta la conversión implícita
        // JNIEnv** → void** o el cast explícito, y OpenJDK lo requiere).
#ifdef __ANDROID__
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0.f;
#else
        if (g_jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) return 0.f;
#endif
        attached = true;
    }
    jstring jkey = env->NewStringUTF(param_key);
    jfloat v = 0.f;
    if (jkey) {
        v = env->CallStaticFloatMethod(g_learningBias_cls, g_learningBias_getBias, jkey);
        env->DeleteLocalRef(jkey);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_jvm->DetachCurrentThread();
    return (float) v;
}

} // extern "C"
