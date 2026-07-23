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
#include <thread>
#include <chrono>

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
#include "../experimental/adaptive_engine/adaptive_decision_engine.hpp"
#include "../perceptual_loudness.hpp"
#include "../ivannalab/ivannalab.h"
#include "omega_shared.h"

// FIX (build roto — ld: undefined symbol: g_shared): g_shared vive
// DENTRO de un namespace anónimo en omega_daemon.cpp (líneas 53-514),
// lo que le da enlace INTERNO (equivalente a 'static' a nivel de
// archivo) — sólo visible dentro de ese translation unit. Un `extern
// OmegaSharedState* g_shared;` directo desde aquí (otro .cpp, mismo
// target `ivanna_omega`, pero OTRA unidad de compilación) nunca podía
// enlazar contra él — de ahí el "ld: error: undefined symbol: g_shared"
// en el build real de la CI, no un problema de CMake ni de targets.
// Se usa en su lugar un accesor con enlace externo real, definido
// FUERA del namespace anónimo al final de omega_daemon.cpp — su cuerpo
// sí puede leer g_shared sin calificar porque comparte translation unit
// con la declaración original (la regla de C++ sólo restringe la
// linkage entre archivos, no el lookup dentro del mismo archivo).
OmegaSharedState* omega_daemon_get_shared_state();

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

// IvannaLab — instancia única, alimentada bajo demanda desde nativeLabFeed().
// No vive en el hot-path de audio de ninguna ruta.
static ivanna::IvannaLab g_lab(96000, 4096);
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

// ═══ FASE 4B: AdaptiveDecisionEngine — cierre del lazo adaptativo ════════════
// Única instancia del motor. Sus buses (rawMetrics, adaptiveState) son
// SPSC seqlock — el audio thread publica RawAudioMetrics + consume
// AdaptiveState, el hilo de control interno hace el trabajo lento.
// g_adaptiveEngineStarted evita doble start() si nativeInit se llama más
// de una vez (p. ej. cambio de sample rate por reproductor de archivo).
static ivanna::experimental::AdaptiveDecisionEngine g_adaptiveEngine;
static std::atomic<bool> g_adaptiveEngineStarted{false};

// Snapshot del último AdaptiveState publicado — lo leen los JNI getters de
// telemetría (fuera del audio thread) para exponer el ciclo a Kotlin/UI.
// Se actualiza dentro del audio thread justo después de consumeIfNewer().
static std::atomic<float> g_lastAdaptiveTargetGain{1.0f};
static std::atomic<float> g_lastAdaptiveCompAmount{0.0f};
static std::atomic<float> g_lastAdaptiveExcReduction{0.0f};
static std::atomic<float> g_lastAdaptiveSpatialWidth{1.0f};
static std::atomic<float> g_lastAdaptiveSafetyMargin{1.0f};
static std::atomic<float> g_lastAdaptiveVoiceProtect{0.0f};
static std::atomic<float> g_lastRawRms{0.0f};
// Adaptive Control Center: modula el mismo motor adaptativo productivo.
static std::atomic<int>   g_adaptiveUiMode{1};      // 0=OFF,1=NATURAL,2=STUDIO,3=EXTREME
static std::atomic<float> g_adaptiveUiIntensity{50.f};
static std::atomic<float> g_lastRawPeak{0.0f};
static std::atomic<float> g_lastRawGrDb{0.0f};
// Band energies — escritas por nativeProcess (Ruta A) y audioRouteBridgeLoop (Ruta B).
// Leídas por nativeGetBandEnergies() para el AdaptiveDashboard.
// No malloc, no mutex: solo atomic stores en el audio thread y loads en el JNI getter.
static std::atomic<float> g_lastBandLow{0.0f};
static std::atomic<float> g_lastBandMid{0.0f};
static std::atomic<float> g_lastBandHigh{0.0f};
static std::atomic<uint64_t> g_lastAdaptiveApplied{0};

// Snapshot persistente AdaptiveState (independiente del audio callback)
static std::atomic<float> g_adaptiveTargetGainSnapshot{1.0f};
static std::atomic<float> g_adaptiveSpatialSnapshot{1.0f};
static std::atomic<float> g_adaptiveSafetySnapshot{1.0f};
static std::atomic<bool> g_adaptiveSnapshotStarted{false};

static void adaptiveSnapshotLoop() {
    uint64_t seq = 0;

    while (true) {
        ivanna::experimental::AdaptiveState st{};

        if (g_adaptiveEngine.adaptiveState.consumeIfNewer(st, seq)) {
            g_adaptiveTargetGainSnapshot.store(
                st.target_gain, std::memory_order_release);

            g_adaptiveSpatialSnapshot.store(
                st.spatial_width, std::memory_order_release);

            g_adaptiveSafetySnapshot.store(
                st.safety_margin, std::memory_order_release);
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }
}


// ═══ Adaptive Feedback Loop — puente ruta B (Spotify/YouTube/apps de
// terceros vía omega_effect.cpp) ═════════════════════════════════════════
//
// Diagnóstico confirmado (auditoría previa): g_adaptiveEngine.rawMetrics
// sólo recibía datos de la ruta A (IvannaBridgePlayer, DSPBridge_
// nativeProcess) — la ruta B (omega_effect.cpp, el .so system-wide que
// realmente procesa Spotify/YouTube/cualquier app externa) es un binario
// SEPARADO (target CMake `omega_effect`, EXCLUDE_FROM_ALL, cargado por
// audioserver — otro proceso) que nunca podía llamar directo a
// g_adaptiveEngine.rawMetrics.publish() porque literalmente no comparte
// memoria de proceso con libivanna_omega.so.
//
// La única vía real entre ambos procesos ya existía: OmegaSharedState,
// mapeada en ambos procesos vía memfd + SCM_RIGHTS (ver omega_daemon.cpp/
// omega_effect.cpp). omega_effect.cpp ahora escribe ai_raw_rms/ai_raw_peak
// de forma INCONDICIONAL en cada bloque (ver updateRawTelemetry() en
// omega_effect.cpp — antes sólo se actualizaba si el AGC estaba activo,
// que es false por defecto). Este hilo, en el PROCESO DE LA APP (mismo
// .so que g_adaptiveEngine), sondea esa memoria compartida y la traduce
// a RawAudioMetrics.
//
// NO es RT — corre en su propio std::thread dedicado, arrancado UNA vez
// desde nativeInit() (nunca desde un audio callback), cadencia fija de
// 30ms, sin malloc por iteración, sin locks (solo loads atómicos + el
// publish() lock-free ya existente del bus).
//
// LIMITACIÓN DOCUMENTADA, no oculta: RawMetricsBus fue diseñado SPSC (un
// solo escritor). Con este hilo, pasa a tener DOS escritores posibles —
// el audio thread de la ruta A (cuando el reproductor propio está
// activo) y este hilo puente (cuando lo está omega_effect). El seqlock
// evita que un lector vea basura (los reintentos de consumeIfNewer()
// cubren eso), pero si ambos escritores publican en la ventana de
// nanosegundos exacta en que el otro también escribe, un solo ciclo de
// telemetría podría quedar con una combinación de campos de ambas
// fuentes (nunca un crash, nunca memoria corrupta — floats simples). En
// la práctica ambas rutas casi nunca están activas al mismo tiempo (un
// usuario escucha el reproductor propio O Spotify, no ambos a la vez), y
// el próximo ciclo (30-50ms después) se autocorrige. No se resolvió con
// un bus multi-productor propiamente dicho porque eso es alcance nuevo,
// no parte de este cierre de integración.
static std::atomic<bool> g_audioRouteBridgeStarted{false};

static void audioRouteBridgeLoop() {
    // Última lectura vista, para no republicar/loguear si omega_effect no
    // está produciendo audio nuevo ahora mismo (evita contaminar el bus
    // con ceros repetidos cuando Spotify/YouTube no están sonando).
    float lastRms = -1.0f, lastPeak = -1.0f;
    uint64_t frameCounter = 0;
    auto lastLogTime = std::chrono::steady_clock::now();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        OmegaSharedState* shared = omega_daemon_get_shared_state();  // load único, evita TOCTOU
        if (!shared) continue;  // daemon aún no arrancó/mapeó memoria en este proceso

        const float rms  = shared->ai_raw_rms.load(std::memory_order_relaxed);
        const float peak = shared->ai_raw_peak.load(std::memory_order_relaxed);

        // Silencio absoluto sostenido (o sin cambios desde la última
        // lectura) → omega_effect probablemente no está procesando audio
        // real ahora mismo (nadie reproduciendo, o efecto en bypass). No
        // publicar para no pisar telemetría real de la ruta A con ceros.
        const bool hasSignal = rms > 1e-6f || peak > 1e-6f;
        const bool changed    = std::fabs(rms - lastRms) > 1e-6f ||
                                 std::fabs(peak - lastPeak) > 1e-6f;
        lastRms = rms; lastPeak = peak;
        if (!hasSignal && !changed) continue;

        // ai_gain_db sólo es significativo si el AGC del efecto está
        // activo (ai_enabled) — se usa como proxy de gain_reduction SOLO
        // en su excursión negativa (AGC reduciendo por señal fuerte); una
        // excursión positiva (AGC compensando señal débil) no es
        // "reducción" y se descarta a 0.
        float grDb = 0.0f;
        if (shared->ai_enabled.load(std::memory_order_relaxed)) {
            const float gainDb = shared->ai_gain_db.load(std::memory_order_relaxed);
            grDb = gainDb < 0.0f ? -gainDb : 0.0f;
        }

        ivanna::experimental::RawAudioMetrics rawM{};
        rawM.rms               = rms;
        rawM.peak              = peak;
        // FIX (cierre de band energy, Ruta B): antes 0.0f hardcodeado.
        // Ahora viene de BandEnergyMeter (omega_daemon.cpp::processLoop()),
        // 3 filtros IIR reales sobre la señal seca — ver ese archivo.
        rawM.band_low_energy   = shared->ai_band_low.load(std::memory_order_relaxed);
        rawM.band_mid_energy   = shared->ai_band_mid.load(std::memory_order_relaxed);
        rawM.band_high_energy  = shared->ai_band_high.load(std::memory_order_relaxed);
        rawM.gain_reduction_db = grDb;
        rawM.voice_score       = 0.0f;   // omega_effect no corre VoiceProtectionController
        // Exponer band energies Ruta B al JNI getter (AdaptiveDashboard)
        g_lastBandLow .store(rawM.band_low_energy,  std::memory_order_relaxed);
        g_lastBandMid .store(rawM.band_mid_energy,  std::memory_order_relaxed);
        g_lastBandHigh.store(rawM.band_high_energy, std::memory_order_relaxed);
        g_adaptiveEngine.rawMetrics.publish(
            ivanna::experimental::RawMetricsBus::Source::RouteB_OmegaEffect, rawM);

        g_lastRawRms.store(rms,   std::memory_order_relaxed);
        g_lastRawPeak.store(peak, std::memory_order_relaxed);
        g_lastRawGrDb.store(grDb, std::memory_order_relaxed);

        // FIX (Opción A de unificación — paridad de protección Ruta A/Ruta
        // B, ver comentario extenso en omega_shared.h::ai_runtime_gain_mul):
        // hasta acá solo se publicaban métricas HACIA el motor adaptativo.
        // Sin este bloque, las decisiones que el motor calcula a partir de
        // ESTAS MISMAS métricas de Spotify/YouTube nunca volvían a tocar el
        // audio de Spotify/YouTube — se aplicaban únicamente a g_gain de la
        // Ruta A (DSPBridge), que no procesa nada en ese momento. Ahora se
        // lee el AdaptiveState más reciente (el mismo que ya alimenta a la
        // Ruta A) y se escribe target_gain de vuelta al daemon.
        static uint64_t s_lastSeenAdaptiveSeq = 0;
        ivanna::experimental::AdaptiveState st{};
        if (g_adaptiveEngine.adaptiveState.consumeIfNewer(st, s_lastSeenAdaptiveSeq)) {
            shared->ai_runtime_gain_mul.store(
                std::clamp(st.target_gain, 0.5f, 1.0f), std::memory_order_release);
            // FIX (Ruta B — spatial_width sin efecto, gap README): antes solo
            // target_gain volvía al daemon; spatial_width (0..1.5, sugerido
            // por el mismo AdaptiveDecisionEngine) se calculaba pero se
            // perdía. g_widener_b.setWidth() lo aplica en processLoop()
            // (omega_daemon.cpp, paso 6). Clamp [0,2] = rango real de
            // StereoWidener::setWidth(), más ancho que el [0,1.5] emitido.
            shared->ai_runtime_spatial_width.store(
                std::clamp(st.spatial_width, 0.0f, 2.0f), std::memory_order_release);
            // FIX (cableado adaptativo incompleto — mismo patrón que
            // spatial_width, gap real distinto): omega_daemon.cpp SÍ lee
            // ai_runtime_comp_amount/ai_runtime_exciter_red en processLoop()
            // y los aplica vía g_comp_b.setRuntimeAmount()/g_exciter_b.
            // setRuntimeReduction() — y el socket SÍ acepta SET_AI_RUNTIME_
            // COMP/EXCRED — pero nada en la app llamaba nunca a esos
            // comandos, ni este bridge los escribía. compressor_amount y
            // exciter_reduction del ADE (Ruta A, ya calculados hace tiempo)
            // se perdían igual que spatial_width antes del fix anterior.
            // Quedaban congelados en su default (0.0 = sin ajuste extra),
            // así que Spotify/YouTube nunca recibía la compresión/reducción
            // de exciter que el motor adaptativo decide en tiempo real.
            shared->ai_runtime_comp_amount.store(
                std::clamp(st.compressor_amount, 0.0f, 1.0f), std::memory_order_release);
            shared->ai_runtime_exciter_red.store(
                std::clamp(st.exciter_reduction, 0.0f, 1.0f), std::memory_order_release);
        }

        ++frameCounter;

        // Log throttleado a ~1/s — este hilo NO es RT, loguear aquí es
        // seguro (a diferencia de dentro de Effect_Process/nativeProcess).
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count() >= 1000) {
            lastLogTime = now;
            __android_log_print(ANDROID_LOG_INFO, "IVANNA.AudioRoute",
                "source=omega_effect frames=%llu rms=%.4f peak=%.4f gr_db=%.2f adaptive_connected=%d",
                (unsigned long long)frameCounter, rms, peak, grDb,
                g_adaptiveEngine.running() ? 1 : 0);
        }
    }
}

static inline float adaptive_mode_base_strength(int mode) noexcept {
    switch (mode) {
        case 0: return 0.0f;
        case 1: return 0.35f;
        case 2: return 0.65f;
        case 3: return 1.0f;
        default: return 0.35f;
    }
}

static inline float adaptive_ui_strength() noexcept {
    const float intensity = std::clamp(
        g_adaptiveUiIntensity.load(std::memory_order_relaxed), 0.f, 100.f) * 0.01f;
    const int mode = g_adaptiveUiMode.load(std::memory_order_relaxed);
    return std::clamp(adaptive_mode_base_strength(mode) * intensity, 0.f, 1.f);
}

static inline float blend_adaptive_from_neutral(float neutral, float suggestion, float strength) noexcept {
    return neutral + (suggestion - neutral) * strength;
}

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

    // ═══ FASE 4B: arrancar el motor adaptativo (una sola vez) ═════════════
    // start() crea un std::thread propio (el hilo de control lento) que
    // corre controlLoop() a 50ms. NO se dispara desde el audio thread
    // — este nativeInit siempre se llama desde el hilo de UI o el hilo
    // que abre el player (ver IvannaBridgePlayer.play() y
    // AudioForegroundService.onStartCommand()). exchange() garantiza
    // idempotencia si el sample rate cambia y nativeInit se re-llama.
    if (!g_adaptiveEngineStarted.exchange(true, std::memory_order_acq_rel)) {
        g_adaptiveEngine.start();

      if (!g_adaptiveSnapshotStarted.exchange(true)) {
          std::thread(adaptiveSnapshotLoop).detach();
          LOGI("AdaptiveState snapshot consumer started");
      }
        LOGI("AdaptiveDecisionEngine started (control thread @50ms)");
    }

    // FIX (Adaptive Feedback Loop — ruta real Spotify/YouTube): arrancar el
    // puente hacia omega_effect.cpp UNA sola vez, mismo guard que el motor
    // adaptativo (nativeInit puede re-llamarse por cambio de sample rate).
    // std::thread se detach() a propósito — vive todo el ciclo de vida del
    // proceso de la app, igual que el hilo de control del propio
    // AdaptiveDecisionEngine y el watchdog de omega_daemon.cpp.
    if (!g_audioRouteBridgeStarted.exchange(true, std::memory_order_acq_rel)) {
        std::thread(audioRouteBridgeLoop).detach();
        LOGI("AudioRoute bridge started (omega_effect -> AdaptiveDecisionEngine, @30ms)");
    }

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

    // ═══ P0 (cierre del Adaptive Feedback Loop): target_gain/compressor_amount/
    // exciter_reduction ahora se aplican a los módulos DSP REALES
    // (GainStage/Compressor/HarmonicExciter), no como ajustes paralelos
    // post-hoc. Suavizado EMA aquí (thread_local, igual patrón que
    // spatial_width más abajo) antes de pasar el valor a cada setter —
    // GainStage ya tiene su propio smoothing interno para el multiplicador
    // final, pero suavizar la SUGERENCIA en sí evita saltos audibles entre
    // bloques de 50ms cuando consumeIfNewer() trae un valor nuevo del hilo
    // de control.
    static thread_local float s_targetGainSmooth = 1.0f;
    static thread_local float s_compAmountSmooth = 0.0f;
    static thread_local float s_excReductionSmooth = 0.0f;
    const float adaptiveStrength = adaptive_ui_strength();
    const float targetGainUi = blend_adaptive_from_neutral(
        1.0f,
        std::clamp(g_lastAdaptiveTargetGain.load(std::memory_order_relaxed), 0.5f, 1.0f),
        adaptiveStrength);
    const float compAmountUi = blend_adaptive_from_neutral(
        0.0f,
        std::clamp(g_lastAdaptiveCompAmount.load(std::memory_order_relaxed), 0.f, 1.f),
        adaptiveStrength);
    const float excReductionUi = blend_adaptive_from_neutral(
        0.0f,
        std::clamp(g_lastAdaptiveExcReduction.load(std::memory_order_relaxed), 0.f, 1.f),
        adaptiveStrength);
    s_targetGainSmooth += 0.05f * (targetGainUi - s_targetGainSmooth);
    s_compAmountSmooth += 0.05f * (compAmountUi - s_compAmountSmooth);
    s_excReductionSmooth += 0.05f * (excReductionUi - s_excReductionSmooth);

    g_gain.setRuntimeGain(s_targetGainSmooth);
    g_comp.setRuntimeAmount(s_compAmountSmooth);
    g_comp.process(chL, chR, n);
    g_exciter.setRuntimeReduction(s_excReductionSmooth);
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
    // FIX (colisión real encontrada en auditoría — mismo patrón que el
    // compresor): esta medición de correlación y la sugerencia de
    // AdaptiveDecisionEngine (más abajo) son dos señales independientes
    // que responden la misma pregunta ("¿hay que ensanchar esto?"). Antes
    // se aplicaban como DOS pasadas M/S separadas y secuenciales sobre el
    // mismo pdOutL/pdOutR — no rompía nada audible de forma catastrófica
    // (M/S es lineal, dos pasadas ≈ una con producto de multiplicadores),
    // pero sí compone sin coordinación y puede sobre-ensanchar. Ahora se
    // mide acá y se combina en UN solo punto de aplicación, junto al
    // resto del ciclo adaptativo (ver más abajo, sección FASE 4B).
    float widenAmountFromCorrelation = 0.f;
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
        widenAmountFromCorrelation = std::clamp((corrSmooth - 0.8f) / 0.2f, 0.f, 1.f) * 0.4f;
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

    // ═══════════════════════════════════════════════════════════
    // FASE 4B: ciclo adaptativo cerrado. Publicar RawAudioMetrics ANTES
    // del re-intercalado + consumir AdaptiveState y aplicar UN parámetro
    // audible (spatial_width sugerido, vía M/S post-scaling encima de
    // pdOutL/pdOutR). No se toca la cadena DSP existente ni SafetyLimiter
    // — solo un ajuste M/S adicional al final, como el ensanche por
    // correlación de arriba. Cero malloc, cero mutex, cero I/O, cero FFT.
    // ═══════════════════════════════════════════════════════════
    {
        // 1) RMS + Peak sobre la salida real (pdOutL/pdOutR), un solo
        //    pase, cero asignaciones. Igual filosofía que el bloque de
        //    correlación L/R de más arriba — se acumula en doubles y se
        //    reduce a float al final.
        double sumSq = 0.0;
        float peakAbs = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float l = pdOutL[i];
            const float r = pdOutR[i];
            sumSq += (double)l * l + (double)r * r;
            const float al = std::fabs(l);
            const float ar = std::fabs(r);
            if (al > peakAbs) peakAbs = al;
            if (ar > peakAbs) peakAbs = ar;
        }
        const float rms = (float)std::sqrt(sumSq / (double)(2 * std::max(n, 1)));

        // 2) Convertir GR lineal → dB con la función pura del motor. Nunca
        //    enviar valor lineal como dB (mismatch documentado en el hpp).
        const float grLin = g_safety_limiter.getGainReduction();
        const float grDb = ivanna::experimental::AdaptiveDecisionEngine
                            ::gainReductionLinearToDb(grLin);

        // 3) Voice score real (VoiceProtectionController → YAMNet TFLite).
        const float vpScore = g_voice_protect_score.load(std::memory_order_relaxed);

        // 4) Publicar. Es un memcpy de POD atrás de un seqlock, no bloquea.
        //    Band energy: NO se agrega análisis nuevo — se reutilizan los
        //    envelopes IIR de 8 bandas (80/200/500/1k/2k/4k/8k/16kHz) que
        //    BiquadEnvelopeBank YA calcula dentro de g_pd.process_block()
        //    (arriba, esta misma función) para el Kernel Evolutivo/PhaseOracle.
        //    Bucketing: low=bandas 0-1 (80-200Hz), mid=2-4 (500Hz-2kHz),
        //    high=5-7 (4k-16kHz) — convención estándar de ingeniería de audio.
        //    Antes: hardcodeado en 0.0f — computeExciterReduction() (que
        //    depende de band_high_energy para detectar sibilancia) operaba
        //    a ciegas en la Ruta A pese a que el dato ya existía, calculado,
        //    dos líneas más arriba en el mismo bloque de audio.
        auto bandEnergy = [&](int lo, int hi) noexcept -> float {
            float sum = 0.f;
            for (int b = lo; b <= hi; ++b) {
                sum += g_pd.cue_bank.envL[b] + g_pd.cue_bank.envR[b];
            }
            return sum / (2.f * (hi - lo + 1));
        };
        ivanna::experimental::RawAudioMetrics rawM{};
        rawM.rms               = rms;
        rawM.peak              = peakAbs;
        rawM.band_low_energy   = bandEnergy(0, 1);
        rawM.band_mid_energy   = bandEnergy(2, 4);
        rawM.band_high_energy  = bandEnergy(5, 7);
        rawM.gain_reduction_db = grDb;
        rawM.voice_score       = vpScore;
        // Exponer band energies al JNI getter (AdaptiveDashboard)
        g_lastBandLow .store(rawM.band_low_energy,  std::memory_order_relaxed);
        g_lastBandMid .store(rawM.band_mid_energy,  std::memory_order_relaxed);
        g_lastBandHigh.store(rawM.band_high_energy, std::memory_order_relaxed);
        g_adaptiveEngine.rawMetrics.publish(
            ivanna::experimental::RawMetricsBus::Source::RouteA_BridgePlayer, rawM);

        // Snapshot para telemetría (getters JNI, fuera del audio thread).
        g_lastRawRms.store(rms,     std::memory_order_relaxed);
        g_lastRawPeak.store(peakAbs, std::memory_order_relaxed);
        g_lastRawGrDb.store(grDb,    std::memory_order_relaxed);

        // 5) Consumir el último AdaptiveState publicado por el hilo de
        //    control (lock-free, no bloquea si no hay uno nuevo). El seq
        //    local persiste en thread_local (audio thread es único caller).
        static thread_local uint64_t s_lastAdaptiveSeq = 0;
        ivanna::experimental::AdaptiveState st;
        if (g_adaptiveEngine.adaptiveState.consumeIfNewer(st, s_lastAdaptiveSeq)) {
            g_lastAdaptiveTargetGain .store(st.target_gain,             std::memory_order_relaxed);
            g_lastAdaptiveCompAmount .store(st.compressor_amount,       std::memory_order_relaxed);
            g_lastAdaptiveExcReduction.store(st.exciter_reduction,       std::memory_order_relaxed);
            g_lastAdaptiveSpatialWidth.store(st.spatial_width,           std::memory_order_relaxed);
            g_lastAdaptiveSafetyMargin.store(st.safety_margin,           std::memory_order_relaxed);
            g_lastAdaptiveVoiceProtect.store(st.voice_protection_amount, std::memory_order_relaxed);
            g_lastAdaptiveApplied.fetch_add(1, std::memory_order_relaxed);
        }

        // 6) APLICAR UN parámetro audible: spatial_width, combinando DOS
        //    señales en un único punto (fix de colisión, ver comentario
        //    junto a la medición de correlación L/R más arriba):
        //      a) widenAmountFromCorrelation — contenido casi-mono real.
        //      b) sugerencia de AdaptiveDecisionEngine (target_gain/
        //         safety_margin ya influyen su cálculo, ver
        //         computeSpatialWidth() en adaptive_decision_engine.cpp).
        //    Se combina con max(), no con multiplicación ni suma — así
        //    cualquiera de las dos señales que pida más ancho gana, sin
        //    componer un sobre-ensanchamiento cuando ambas piden ensanchar
        //    al mismo tiempo. Encoding M/S: pdOut = mid + side*sideMul.
        //    Smoothing exponencial (thread_local) para evitar clics.
        static thread_local float s_widthSmooth = 1.0f;
        const float widthTarget = blend_adaptive_from_neutral(
            1.0f,
            std::clamp(g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed), 0.f, 1.5f),
            adaptiveStrength);
        s_widthSmooth += 0.02f * (widthTarget - s_widthSmooth);  // ~50 bloques a τ
        const float adaptiveWidenAmount = std::max(0.f, s_widthSmooth - 1.f);
        const float combinedWidenAmount = std::max(widenAmountFromCorrelation, adaptiveWidenAmount);
        if (combinedWidenAmount > 0.005f) {
            const float sideMul = 1.f + combinedWidenAmount;
            for (int i = 0; i < n; ++i) {
                const float mid  = (pdOutL[i] + pdOutR[i]) * 0.5f;
                const float side = (pdOutL[i] - pdOutR[i]) * 0.5f * sideMul;
                pdOutL[i] = mid + side;
                pdOutR[i] = mid - side;
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
    // Adaptive decisions: mismos atomics que actualiza nativeProcess cuando
    // consumeIfNewer() trae un AdaptiveState nuevo. thread_local smooth
    // independiente (nativeProcessBlock puede correr en thread distinto o
    // el mismo; en ambos casos converge a los mismos valores del ADE).
    // Sin malloc, sin mutex — solo atomic loads + EMA, idéntico patrón al
    // bloque de P0 en nativeProcess.
    static thread_local float s_blk_tgSmooth  = 1.0f;
    static thread_local float s_blk_caSmooth  = 0.0f;
    static thread_local float s_blk_erSmooth  = 0.0f;
    const float adaptiveStrength = adaptive_ui_strength();
    const float blkTargetGain = blend_adaptive_from_neutral(
        1.0f,
        std::clamp(g_lastAdaptiveTargetGain.load(std::memory_order_relaxed), 0.5f, 1.0f),
        adaptiveStrength);
    const float blkCompAmount = blend_adaptive_from_neutral(
        0.0f,
        std::clamp(g_lastAdaptiveCompAmount.load(std::memory_order_relaxed), 0.f, 1.f),
        adaptiveStrength);
    const float blkExcReduction = blend_adaptive_from_neutral(
        0.0f,
        std::clamp(g_lastAdaptiveExcReduction.load(std::memory_order_relaxed), 0.f, 1.f),
        adaptiveStrength);
    s_blk_tgSmooth += 0.05f * (blkTargetGain - s_blk_tgSmooth);
    s_blk_caSmooth += 0.05f * (blkCompAmount - s_blk_caSmooth);
    s_blk_erSmooth += 0.05f * (blkExcReduction - s_blk_erSmooth);

    g_gain.processInput(lBuf, rBuf, n);
    g_eq.process(lBuf, rBuf, n);
    g_gain.setRuntimeGain(s_blk_tgSmooth);
    g_comp.setRuntimeAmount(s_blk_caSmooth);
    g_comp.process(lBuf, rBuf, n);
    g_exciter.setRuntimeReduction(s_blk_erSmooth);
    g_exciter.process(lBuf, rBuf, n);
    g_widener.process(lBuf, rBuf, n);
    g_gain.processOutput(lBuf, rBuf, n);
    // SafetyLimiter: faltaba en esta ruta. nativeProcess lo aplica; sin él
    // aquí bloques que superen 0 dBFS salían sin protección hacia el DAC.
    g_safety_limiter.process(lBuf, rBuf, n);

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


JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetClipCount(JNIEnv*, jobject) {
    return (jint)g_safety_limiter.getClipCount();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeResetClipCount(JNIEnv*, jobject) {
    g_safety_limiter.resetClipCount();
}

// ═══════════════════════════════════════════════════════════════════════════
// IvannaLab — puente JNI (nativeLabFeed espera [L0,R0,L1,R1,...] intercalado;
// nativeLabMeasure devuelve 7 floats en el orden de LabResult, ivannalab.h:
// [0]=thdPercent [1]=imdPercent [2]=integratedLUFS [3]=luRange [4]=snrDB
// [5]=peakDBFS [6]=truepeakDBTP. -1.0f = no medido/datos insuficientes.
// ═══════════════════════════════════════════════════════════════════════════
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLabReset(JNIEnv*, jobject) {
    g_lab.reset();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLabFeed(
    JNIEnv* env, jobject, jfloatArray interleavedStereo, jint frames) {
    if (!interleavedStereo || frames <= 0) return;
    jfloat* p = env->GetFloatArrayElements(interleavedStereo, nullptr);
    if (!p) return;
    const jsize len = env->GetArrayLength(interleavedStereo);
    const int maxFrames = static_cast<int>(len / 2);
    g_lab.feed(p, std::min(static_cast<int>(frames), maxFrames));
    env->ReleaseFloatArrayElements(interleavedStereo, p, JNI_ABORT);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLabMeasure(JNIEnv* env, jobject) {
    const ivanna::LabResult r = g_lab.measure();
    jfloatArray out = env->NewFloatArray(7);
    if (!out) return nullptr;
    const jfloat vals[7] = {
        r.thdPercent, r.imdPercent, r.integratedLUFS, r.luRange,
        r.snrDB, r.peakDBFS, r.truepeakDBTP
    };
    env->SetFloatArrayRegion(out, 0, 7, vals);
    return out;
}

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLabReport(JNIEnv* env, jobject) {
    return env->NewStringUTF(g_lab.generateReport().c_str());
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

// FIX CRÍTICO DE REGRESIÓN: esta función desapareció de una reescritura en
// paralelo de este archivo, pero IvannaNativeLib.kt (Kotlin) sigue
// declarando "external fun nativeSetEQParams(...)" y AdaptiveBackend.kt la
// sigue llamando en cada movimiento de slider de EQ — sin este symbol el
// primer toque a un slider tira UnsatisfiedLinkError y crashea la app.
// Motivo original del fix (ver AdaptiveBackend.kt): nativeSetParams(FloatArray)
// de abajo sobreescribe TODO g_params y dispara setParams() en
// g_eq+g_comp+g_exciter+g_widener+g_gain — si el caller solo llena
// low/mid/high/master (como hacía la versión vieja de applyEQ), el resto
// llega en 0 y apaga comp/exciter. Este setter solo toca esos 4 campos y
// solo reconfigura g_eq/g_gain.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEQParams(
    JNIEnv*, jobject,
    jfloat low, jfloat mid, jfloat high, jfloat master) {
    g_params.low    = low;
    g_params.mid    = mid;
    g_params.high   = high;
    g_params.master = master;
    g_eq.setParams(g_params);
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

// Compresor (GlassCard "COMPRESOR"): threshold en dB [-24..0], ratio [1..20]:1,
// attack/release en ms — extendido para el control adaptativo @10Hz que ya
// los pasaba (MainActivity.kt) mientras el JNI solo aceptaba 2 args (build
// roto en CI: "Too many arguments"). setAttack()/setRelease() ya existían
// en Compressor.h, solo faltaba exponerlos acá.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCompressorParams(
    JNIEnv*, jobject, jfloat thresholdDb, jfloat ratio, jfloat attackMs, jfloat releaseMs) {
    g_comp.setThreshold(thresholdDb);
    g_comp.setRatio(ratio);
    g_comp.setAttack(attackMs);
    g_comp.setRelease(releaseMs);
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

// ═══════════════════════════════════════════════════════════════════════════
// FIX (Motor B / orphan JNI 2026-07-22): las 4 external fun de Kotlin
//   IvannaNativeLib.nativeSetAdaptiveEngineEnabled
//   IvannaNativeLib.nativeCreateAdaptiveEngine
//   IvannaNativeLib.nativeGetAdaptiveParameters
//   IvannaNativeLib.nativeGetAudioCharacteristics
// llegaron al main SIN implementación JNI real. Historial reconstruido:
//   1. c089b34 las implementó aquí (delegando al Motor A real).
//   2. fc3346a las renombró a *_unused en ivanna_adaptive_jni.cpp para
//      resolver la colisión de símbolos.
//   3. Una reescritura paralela posterior (293b885 menciona regresión)
//      eliminó las 3 de aquí; los *_unused en ivanna_adaptive_jni.cpp
//      quedaron como funciones C++ regulares sin JNIEXPORT.
// Resultado: 4 orphans (los 3 anteriores + nativeSetAdaptiveEngineEnabled
// que nunca tuvo implementación) — MainActivity.kt y AdaptiveEngineScreen.kt
// tienen callers reales que fallan silenciosamente vía try/catch
// UnsatisfiedLinkError → el motor adaptativo B no hace nada aunque el
// toggle esté encendido.
//
// Restauración (regla de oro: no borrar, revivir): se re-cablean los 4
// exports JNI a la fuente de verdad del Motor A (los mismos atomics
// g_lastAdaptive* que ya lee nativeGetAdaptiveTelemetry). Cero duplicación
// de estado. ivanna_adaptive_jni.cpp queda intacto — sus stubs *_unused
// siguen ahí por si alguien decide revivir AdaptiveEngineCore como
// sensor independiente en el futuro (ver INTEGRATION_GUIDE.md).
// ═══════════════════════════════════════════════════════════════════════════

// Flag para el toggle Manual/Automático (pausa/reanuda el loop del ADE).
// El motor A ya expone start()/stop() en g_adaptiveEngine — solo cableamos
// el switch para que llame al método correcto según el estado.
static std::atomic<bool> g_adaptiveEngineUiEnabled{true};

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptiveEngineEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool want = (enabled == JNI_TRUE);
    const bool prev = g_adaptiveEngineUiEnabled.exchange(want, std::memory_order_acq_rel);
    if (prev == want) return;  // idempotente — evita start/stop dobles
    if (want) {
        // Reanuda el hilo de control del Motor A. Si ya está corriendo (por
        // nativeInit), start() debe ser no-op — g_adaptiveEngineStarted lo
        // garantiza.
        if (!g_adaptiveEngineStarted.exchange(true, std::memory_order_acq_rel)) {
            g_adaptiveEngine.start();
        }
    } else {
        // Pausa el loop del ADE para que el modo manual pueda escribir
        // compresor/exciter/ancho sin colisionar. Se resetea el flag
        // "started" para que un enable(true) posterior lo re-arranque.
        if (g_adaptiveEngineStarted.exchange(false, std::memory_order_acq_rel)) {
            g_adaptiveEngine.stop();
        }
    }
}

// Crea/asegura la instancia del Adaptive Engine. En la arquitectura actual
// g_adaptiveEngine es un objeto estático global — no hay handle real que
// devolver, pero la firma Kotlin exige un Long. Se devuelve la dirección
// del singleton (opaco al lado Kotlin, solo se usa como "non-zero = OK").
// Idempotente: llamadas repetidas devuelven el mismo puntero sin re-crear.
JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeCreateAdaptiveEngine(
    JNIEnv*, jobject) {
    // Asegura que el motor esté corriendo (mismo path que nativeInit toma).
    if (!g_adaptiveEngineStarted.exchange(true, std::memory_order_acq_rel)) {
        g_adaptiveEngine.start();
    }
    g_adaptiveEngineUiEnabled.store(true, std::memory_order_release);
    return reinterpret_cast<jlong>(&g_adaptiveEngine);
}

// Devuelve los 12 parámetros adaptativos suavizados. En el Motor A los
// atomics g_lastAdaptive* + los snapshots de AdaptiveState son la fuente
// de verdad. El AdaptiveEngineCore original devolvía 12 campos (compressor
// threshold/ratio/attack/release + exciter + width + EQ 3 bandas + gain +
// spatial + safety); mapeamos los que el Motor A sí calcula y dejamos en
// valores neutros los que no aplican (EQ per-band no existe como salida
// adaptativa en el Motor A, se controla desde YAMNet/route en su lugar).
// Firma Kotlin (IvannaNativeLib.kt) — ORDEN EXACTO:
//   [0]  compressor_threshold (dB)
//   [1]  compressor_ratio
//   [2]  exciter_amount (0..1)
//   [3]  stereo_width (0..2)
//   [4]  eq_bass (dB)
//   [5]  eq_mid (dB)
//   [6]  eq_treble (dB)
//   [7]  overall_gain (master, lineal)
//   [8]  compressor_attack (ms)
//   [9]  compressor_release (ms)
//   [10] spatial_intensity (0..1)
//   [11] safety_margin (0..1)
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAdaptiveParameters(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(12);
    if (!arr) return nullptr;

    // Motor A: compAmount está normalizado 0..1. Se re-mapea a threshold/
    // ratio de referencia para que la UI del Motor B siga leyendo unidades
    // coherentes (mismo mapeo que usa el DSP interno):
    //   threshold_db = -6 - compAmount * 18   → [-6..-24] dB
    //   ratio        = 1 + compAmount * 7     → [1..8]:1
    const float compAmount    = g_lastAdaptiveCompAmount.load(std::memory_order_relaxed);
    const float excReduction  = g_lastAdaptiveExcReduction.load(std::memory_order_relaxed);
    const float targetGain    = g_lastAdaptiveTargetGain.load(std::memory_order_relaxed);
    const float spatialWidth  = g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed);
    const float safetyMargin  = g_lastAdaptiveSafetyMargin.load(std::memory_order_relaxed);

    float v[12];
    v[0]  = -6.0f - compAmount * 18.0f;               // compressor_threshold (dB)
    v[1]  = 1.0f + compAmount * 7.0f;                 // compressor_ratio
    // exciter_amount: 1 - excReduction (excReduction=0 → wet completo,
    // excReduction=1 → exciter apagado)
    v[2]  = std::clamp(1.0f - excReduction, 0.f, 1.f);
    v[3]  = std::clamp(spatialWidth, 0.f, 2.f);       // stereo_width
    v[4]  = 0.0f;                                     // eq_bass — no controlado por Motor A
    v[5]  = 0.0f;                                     // eq_mid — idem
    v[6]  = 0.0f;                                     // eq_treble — idem
    v[7]  = std::clamp(targetGain, 0.f, 4.f);         // overall_gain (lineal)
    v[8]  = 10.0f;                                    // compressor_attack (ms) — default DSP
    v[9]  = 100.0f;                                   // compressor_release (ms) — default DSP
    v[10] = std::clamp(spatialWidth * 0.5f, 0.f, 1.f); // spatial_intensity
    v[11] = std::clamp(safetyMargin, 0.f, 1.f);       // safety_margin
    env->SetFloatArrayRegion(arr, 0, 12, v);
    return arr;
}

// Devuelve las 8 características analizadas del audio. En el Motor A las
// métricas primarias las publica el audio thread cada bloque a los atomics
// g_lastRawRms/Peak/GrDb; percussiveness/tonality/reverb no las calcula el
// Motor A directamente pero se derivan de la razón peak/rms + banda alta
// del BiquadEnvelopeBank (fuente ya viva en el DSP).
// Firma Kotlin — ORDEN EXACTO:
//   [0] rms
//   [1] peak
//   [2] percussiveness (0..1)
//   [3] tonality (0..1)
//   [4] reverb_amount (0..1)
//   [5] dynamic_range (0..1)
//   [6] spectral_centroid (Hz)
//   [7] spectral_spread (Hz)
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAudioCharacteristics(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(8);
    if (!arr) return nullptr;

    const float rms    = g_lastRawRms.load(std::memory_order_relaxed);
    const float peak   = g_lastRawPeak.load(std::memory_order_relaxed);
    const float bandLo = g_lastBandLow.load(std::memory_order_relaxed);
    const float bandMi = g_lastBandMid.load(std::memory_order_relaxed);
    const float bandHi = g_lastBandHigh.load(std::memory_order_relaxed);

    // Percussiveness: crest factor normalizado. Ratio peak/rms alto ⇒ ataques
    // fuertes (batería, transientes); ratio bajo ⇒ señal sostenida (pad, voz).
    // Se mapea [1..8] crest → [0..1] percussiveness con clamp.
    const float crest = (rms > 1e-6f) ? (peak / rms) : 1.0f;
    const float percussiveness = std::clamp((crest - 1.0f) / 7.0f, 0.f, 1.f);

    // Tonality: energía media-alta / energía total. Música tonal tiene
    // distribución equilibrada; ruido colapsa a plano espectral.
    const float bandSum = bandLo + bandMi + bandHi;
    const float tonality = (bandSum > 1e-6f)
        ? std::clamp((bandMi + bandHi * 0.5f) / bandSum, 0.f, 1.f)
        : 0.0f;

    // Reverb amount: aproximado por la razón GR (compresión) vs. dinámica
    // real. Motor A no tiene detector de reverb dedicado — se deja proxy.
    const float grDb = g_lastRawGrDb.load(std::memory_order_relaxed);
    const float reverbApprox = std::clamp(std::abs(grDb) / 12.0f, 0.f, 1.f);

    // Dynamic range: inverso normalizado de la compresión aplicada.
    // Motor A no lo mide de forma independiente, se aproxima igual.
    const float dynamicRange = 1.0f - std::clamp(std::abs(grDb) / 24.0f, 0.f, 1.f);

    // Spectral centroid/spread: aproximado con las 3 bandas Gammatone que
    // el Motor A sí publica (low ~120 Hz, mid ~1500 Hz, high ~8000 Hz).
    // Centroid = Σ(f_i * E_i) / Σ(E_i)
    const float f_lo = 120.0f, f_mi = 1500.0f, f_hi = 8000.0f;
    const float centroid = (bandSum > 1e-6f)
        ? (f_lo * bandLo + f_mi * bandMi + f_hi * bandHi) / bandSum
        : 0.0f;
    // Spread ~ desviación de las bandas respecto al centroid
    float spread = 0.0f;
    if (bandSum > 1e-6f) {
        const float dLo = f_lo - centroid, dMi = f_mi - centroid, dHi = f_hi - centroid;
        const float var = (dLo * dLo * bandLo + dMi * dMi * bandMi + dHi * dHi * bandHi) / bandSum;
        spread = std::sqrt(std::max(0.f, var));
    }

    float v[8];
    v[0] = rms;
    v[1] = peak;
    v[2] = percussiveness;
    v[3] = tonality;
    v[4] = reverbApprox;
    v[5] = dynamicRange;
    v[6] = centroid;
    v[7] = spread;
    env->SetFloatArrayRegion(arr, 0, 8, v);
    return arr;
}

// ─── FASE 4B: telemetría del ciclo adaptativo real ──────────────────────
// Devuelve un snapshot POD de 10 floats con: [rms, peak, gr_db, target_gain,
// comp_amount, exc_reduction, spatial_width, safety_margin, voice_protect,
// adaptive_applied_count]. Se llena fuera del audio thread desde los
// atomics que el audio thread ya actualiza cada bloque. Uso desde Kotlin
// con throttle (recomendado ≥500 ms) — este getter en sí no throttlea,
// para que la UI decida la cadencia.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptiveControls(
    JNIEnv*, jobject, jint modeOrdinal, jfloat intensityPercent) {
    g_adaptiveUiMode.store(std::clamp((int)modeOrdinal, 0, 3), std::memory_order_relaxed);
    g_adaptiveUiIntensity.store(std::clamp((float)intensityPercent, 0.f, 100.f), std::memory_order_relaxed);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAdaptiveTelemetry(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(10);
    if (!arr) return nullptr;
    float v[10];
    v[0] = g_lastRawRms.load(std::memory_order_relaxed);
    v[1] = g_lastRawPeak.load(std::memory_order_relaxed);
    v[2] = g_lastRawGrDb.load(std::memory_order_relaxed);
    v[3] = g_lastAdaptiveTargetGain.load(std::memory_order_relaxed);
    v[4] = g_lastAdaptiveCompAmount.load(std::memory_order_relaxed);
    v[5] = g_lastAdaptiveExcReduction.load(std::memory_order_relaxed);
    v[6] = g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed);
    v[7] = g_lastAdaptiveSafetyMargin.load(std::memory_order_relaxed);
    v[8] = g_lastAdaptiveVoiceProtect.load(std::memory_order_relaxed);
    v[9] = (float) g_lastAdaptiveApplied.load(std::memory_order_relaxed);
    env->SetFloatArrayRegion(arr, 0, 10, v);
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsAdaptiveEngineRunning(
    JNIEnv*, jobject) {

    const bool active =
        g_initialized.load(std::memory_order_acquire) &&
        (
            g_adaptiveEngineStarted.load(std::memory_order_acquire) ||
            g_lastAdaptiveApplied.load(std::memory_order_relaxed) > 0
        );

    return active ? JNI_TRUE : JNI_FALSE;
}

// ── nativeGetBandEnergies — expone band energies al AdaptiveDashboard ─────────
// FloatArray[3]: [0]=low (sub/bass), [1]=mid (presencia/voz), [2]=high (brillo/sibilancia)
// Valores en amplitud lineal RMS normalizada. 0.0 = silencio, 1.0 = clip level.
// Escritas por nativeProcess (Ruta A vía BiquadEnvelopeBank de PDEngine) y
// audioRouteBridgeLoop (Ruta B vía 3 IIR bandpass en omega_daemon).
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetBandEnergies(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(3);
    if (!arr) return nullptr;
    const float v[3] = {
        g_lastBandLow .load(std::memory_order_relaxed),
        g_lastBandMid .load(std::memory_order_relaxed),
        g_lastBandHigh.load(std::memory_order_relaxed)
    };
    env->SetFloatArrayRegion(arr, 0, 3, v);
    return arr;
}

} // extern "C"
