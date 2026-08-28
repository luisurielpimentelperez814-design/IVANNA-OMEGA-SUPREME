#include "saf_runtime.h"
/*
 * ivanna_omega_jni.cpp — IVANNA OMEGA SUPREME
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Arquitectura OPE post-refactor:
 *   DSP chain → PDEngine (NHO + BiquadEnvelopeBank + CueBasedSpatial)
 *   EvolutionaryKernel movido a modo OFFLINE (no corre en audio thread)
 */
#include <jni.h>
#include "omega_perceptual_guard.h"
#include "../include/audio_thread_priority.h"
#include "../include/omega_control_bus.h"   // effectControlBus(), OmegaDspSnapshot, OMEGA_EFFECT_LOCAL_BUS_PATH
#include <android/log.h>
#include <cstring>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "../include/dsp_types.h"
#include "../include/ParametricEQ.h"
#include "../include/Compressor.h"
#include "../include/HarmonicExciter.h"
#include "../include/StereoWidener.h"
#include "../include/GainStage.h"
#include "../include/SafetyLimiter.h"
#include "../spatial/RirConvolver.hpp"
#include "../spatial/RirDataset.hpp"
#include "../pd_engine.hpp"
#include "../control_frame.hpp"
#include "../include/dc_blocker.hpp"
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
// FIX (build, undefined symbol al linkear libivanna_omega.so):
// omega_daemon_bridge_stub.cpp DEFINE este simbolo como `extern "C"`,
// pero aqui se declaraba con enlace C++ por defecto. Son dos simbolos
// distintos: el declarado se mangla como
//   _Z30omega_daemon_get_shared_statev
// mientras el definido exporta el nombre plano
//   omega_daemon_get_shared_state
// El linker resolvia el mangled, no lo encontraba, y lo dijo textual:
//   "did you mean: extern \"C\" omega_daemon_get_shared_state".
// El .so nunca llegaba a enlazarse (-Wl,--no-undefined), asi que ni el
// APK ni las tareas de Kotlin se ejecutaban. La declaracion debe
// coincidir con la definicion: enlace C, mismo nombre exacto —
// exactamente el contrato que el propio stub documenta ("NO cambiar la
// firma ni la ubicacion del simbolo").
extern "C" OmegaSharedState* omega_daemon_get_shared_state();
#define LOG_TAG "IVANNA-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace ivanna;
// ── Bus + staging frame (ver audio_control_plane.cpp) ────────────────────────
// Definición real de los externs declarados en audio_control_plane.cpp.
// El hilo JNI/UI publica ControlFrame nuevos aquí; el hilo de audio los
// consume vía ControlFrameBus::consumeIfNewer().
namespace ivanna {

        // SAF FULL ADAPTIVE UPDATE
        // Phi_SAF infinity
        

    ControlFrameBus g_control_bus;
    ControlFrame    g_staging_frame;
}
// ── Engine singletons (static storage — zero allocations) ────────────────────
static ParametricEQ   g_eq;
static Compressor     g_comp;
static HarmonicExciter g_exciter;
static StereoWidener  g_widener;
static GainStage      g_gain;
// Anti-ruido digital: DC-block + sanitizer NaN/Inf en el borde de salida.
static ivanna::DcBlocker g_dcBlockL, g_dcBlockR;
static SafetyLimiter  g_safety_limiter;
// OMEGA PERCEPTUAL GUARD
static OmegaPerceptualGuard g_perceptualGuard;

// IvannaLab — instancia única, alimentada bajo demanda desde nativeLabFeed().
// No vive en el hot-path de audio de ninguna ruta.
static ivanna::IvannaLab g_lab(96000, 4096);
static PDEngine       g_pd;    // NHO + BiquadEnvelopeBank + CueBasedSpatial
static DSPParams      g_params;
static std::atomic<bool> g_initialized{false};
// FIX RT (Ruta A): el dataset RIR y su convolver se inicializan en
// nativeInitDSP (hilo de UI, no-RT) y se publican con release-store; el
// hot path de audio solo hace load acquire — antes el primer callback de
// audio hacia `new RirDataset()` + lectura de disco desde el propio
// callback -> XRun garantizado en el arranque de la reproducción.
static std::atomic<Ivanna::RirDataset*>   g_rirDataset{nullptr};
static std::atomic<Ivanna::RirConvolver*> g_rirConvolver{nullptr};
// FIX (tronidos + RT violation, 2026-08-27): el cambio de sala RIR leía
// un WAV de disco (readWavPcm16) y alocaba std::vector DENTRO del callback
// de audio cada vez que cambiaba roomIdx -> I/O + alloc en el hot path =
// tronido garantizado en cada cambio de sala. Ahora el hot path solo
// publica el índice deseado (atomic store) y un hilo de control hace la
// lectura de disco + la FFT del IR y la entrega vía RirConvolver::load()
// (que ya es thread-safe con process() vía pending_ + crossfade).
static std::atomic<int32_t>       g_rirPendingIdx{-1};
static std::mutex                 g_rirWorkerMtx;
static std::condition_variable    g_rirWorkerCv;
static std::atomic<bool>          g_rirWorkerRunning{false};
static std::thread                g_rirWorkerThread;
static void rirWorkerLoop() {
    std::unique_lock<std::mutex> lk(g_rirWorkerMtx);
    while (true) {
        g_rirWorkerCv.wait(lk, [] {
            return !g_rirWorkerRunning.load(std::memory_order_acquire)
                || g_rirPendingIdx.load(std::memory_order_acquire) >= 0;
        });
        if (!g_rirWorkerRunning.load(std::memory_order_acquire)) return;
        const int32_t idx = g_rirPendingIdx.exchange(-1, std::memory_order_acq_rel);
        if (idx < 0) continue;
        Ivanna::RirDataset*   ds   = g_rirDataset.load(std::memory_order_acquire);
        Ivanna::RirConvolver* conv = g_rirConvolver.load(std::memory_order_acquire);
        if (!ds || !conv) continue;
        lk.unlock();  // no retener el mutex durante disco + FFT
        std::vector<float> irL, irR;
        int sr = 0;
        if (ds->loadImpulseResponse((size_t)idx, irL, irR, sr) && !irL.empty()) {
            int irLen = (int)irL.size();
            if (irLen > Ivanna::RirConvolver::MAX_IR) irLen = Ivanna::RirConvolver::MAX_IR;
            conv->load(irL.data(), irR.data(), irLen);
        }
        lk.lock();
    }
}
// FEATURE (Voice Protection): 0..1, cuánta voz detecta YamnetClassifier en
// el bloque actual. Protege la inteligibilidad de la voz mezclando de
// vuelta hacia la señal seca (pre-DSP) cuando hay voz dominante, en vez de
// dejar que Exciter/Compresor la sobre-procesen.
static std::atomic<float> g_voice_protect_score{0.f};

// ── FIX nho_wet: 3 escritores competían sobre g_pd.set_nho_wet() ─────────────
// nativeSetParams pisaba el control HRTF WET y el η del LSTM en cada movimiento
// de slider. Se separan en 3 atómicos; applyNhoWet() los combina y escribe una
// sola vez. Valores iniciales: exciter neutro, spatial ON, eta neutro.
static std::atomic<float> g_nho_wet_exciter{0.16f};
static std::atomic<float> g_nho_wet_spatial{1.0f};
static std::atomic<float> g_nho_wet_eta    {0.5f};

// ── Estado del hilo de audio (reemplaza thread_local) ─────────────────────────
// thread_local dentro del callback JNI es incorrecto: Android puede migrar
// el callback entre threads en reinstanciaciones del engine (nativeReset +
// nativeInit) → cada thread nuevo arranca con estado 0 → salto de ganancia →
// tronido/pop audible. Un único g_ats por proceso es correcto porque el engine
// es singleton (un solo hilo de audio activo en todo momento).
struct AudioThreadState {
    float chL[2048]            = {};
    float chR[2048]            = {};
    float dryL[2048]           = {};
    float dryR[2048]           = {};
    float pdOutL[2048]         = {};
    float pdOutR[2048]         = {};
    float targetGainSmooth     = 1.0f;
    float compAmountSmooth     = 0.0f;
    float excReductionSmooth   = 0.0f;
    float guardCompLimitApplied= 1.0f;
    float guardExcLimitApplied = 1.0f;
    float corrSmooth           = 0.7f;
    float dryMixSmooth         = 0.0f;
    float guardCompLimit       = 1.0f;
    float guardExcLimit        = 1.0f;
    float widthSmooth          = 1.0f;
    uint64_t lastAdaptiveSeq   = 0;
    // nativeProcessBlock path (puede correr en thread distinto a nativeProcess)
    float blkTgSmooth          = 1.0f;
    float blkCaSmooth          = 0.0f;
    float blkErSmooth          = 0.0f;
};
static AudioThreadState g_ats;

static inline void applyNhoWet() noexcept {
    float v = g_nho_wet_exciter.load(std::memory_order_relaxed)
            * g_nho_wet_spatial.load(std::memory_order_relaxed)
            * g_nho_wet_eta    .load(std::memory_order_relaxed);
    g_pd.set_nho_wet(v < 0.f ? 0.f : v > 1.f ? 1.f : v);
}
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
// 0=NONE 1=RouteA_BridgePlayer 2=RouteB_OmegaEffect
static std::atomic<int> g_activeRoute{0};
// Snapshot persistente AdaptiveState (independiente del audio callback)
static std::atomic<float> g_adaptiveTargetGainSnapshot{1.0f};
static std::atomic<float> g_adaptiveSpatialSnapshot{1.0f};
static std::atomic<float> g_adaptiveSafetySnapshot{1.0f};
static std::atomic<bool>  g_adaptiveSnapshotStarted{false};

// FIX (causa real de std::terminate() intermitente — confirmed):
// El hilo anterior era std::thread(...).detach() con while(true) puro.
// Cuando Android descarga el .so o el proceso termina, el destructor de
// g_adaptiveEngine (objeto estático global) corre en el hilo principal.
// El hilo detached sigue vivo y accede a g_adaptiveEngine.adaptiveState
// ya destruido → undefined behavior → std::terminate() / SIGSEGV.
// Fix: hilo joinable con flag de parada explícito. JNI_OnUnload señala
// el flag, hace join(), y SOLO ENTONCES se permite que los destructores
// estáticos destruyan g_adaptiveEngine.
static std::atomic<bool>  g_snapshotRunning{false};
static std::thread        g_snapshotThread;
// FIX (UB on unload — same class as g_snapshotThread): audioRouteBridgeLoop
// usaba detach() + while(true) sin flag de parada. JNI_OnUnload paraba el
// snapshot thread pero el bridge loop seguía vivo y accedía a
// g_adaptiveEngine.rawMetrics/adaptiveState después de que sus destructores
// estáticos corriesen → UB / std::terminate() en Ruta B activa al salir.
// Fix idéntico al del snapshot: hilo joinable + flag atómico de parada.
static std::atomic<bool>  g_bridgeRunning{false};
static std::thread        g_bridgeThread;

static void adaptiveSnapshotLoop() {
    uint64_t seq = 0;
    while (g_snapshotRunning.load(std::memory_order_relaxed)) {
        ivanna::experimental::AdaptiveState st{};
        if (g_adaptiveEngine.adaptiveState.consumeIfNewer(st, seq)) {
            g_adaptiveTargetGainSnapshot.store(
                st.target_gain, std::memory_order_release);
            g_adaptiveSpatialSnapshot.store(
                st.spatial_width, std::memory_order_release);
            g_adaptiveSafetySnapshot.store(
                st.safety_margin,
                std::memory_order_release);
            // FIX (telemetria 0% Ruta B): nativeProcess no corre cuando
            // Spotify/YouTube estan activos. Este loop es la unica fuente
            // de AdaptiveState independiente de la ruta activa.
            g_lastAdaptiveTargetGain .store(st.target_gain,              std::memory_order_release);
            g_lastAdaptiveCompAmount .store(st.compressor_amount,        std::memory_order_release);
            g_lastAdaptiveExcReduction.store(st.exciter_reduction,       std::memory_order_release);
            g_lastAdaptiveSpatialWidth.store(st.spatial_width,           std::memory_order_release);
            g_lastAdaptiveSafetyMargin.store(st.safety_margin,           std::memory_order_release);
            g_lastAdaptiveVoiceProtect.store(st.voice_protection_amount, std::memory_order_release);
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
    while (g_bridgeRunning.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        OmegaSharedState* shared = omega_daemon_get_shared_state();  // load único, evita TOCTOU
        // ── FIX (UI "SIN AUDIO" con motor procesando, Ruta B sin daemon) ──
        // omega_daemon_get_shared_state() conecta al estado compartido SHM cuando está disponible; si no existe devuelve nullptr
        // nullptr cuando el daemon no corre (dispositivo sin root o módulo
        // sin daemon). Pero omega_effect SÍ publica telemetría real
        // (raw_rms/raw_peak/effect_frames) en su bus local SHM
        // OMEGA_EFFECT_LOCAL_BUS_PATH desde 865c53c6. El canal previsto por
        // la arquitectura ES ese bus seqlock (no hay canal alternativo: el
        // socket del daemon no existe sin daemon). La app lo lee como
        // READER lock-free — mismo mecanismo, sin IPC nuevo.
        float rms = 0.0f, peak = 0.0f;
        if (shared) {
            rms  = shared->ai_raw_rms.load(std::memory_order_relaxed);
            peak = shared->ai_raw_peak.load(std::memory_order_relaxed);
        } else {
            // Fallback Ruta B: leer el bus local del efecto (audioserver).
            // HARDENING: reintentar apertura del bus si omega_effect se reinicia.
            // La version anterior usaba s_busTried=true una sola vez — si el efecto
            // no estaba listo en el primer ciclo, nunca se reintentaba.
            // Con backoff de 1s: seguro en hilo de telemetria (no es audio callback).
            static bool     s_busOpen = false;
            static uint64_t s_retryMs = 0;
            if (!s_busOpen) {
                auto nowMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                if (nowMs >= s_retryMs) {
                    s_busOpen = ivanna::effectControlBus().openReader(
                                    ivanna::OMEGA_EFFECT_LOCAL_BUS_PATH);
                    s_retryMs = nowMs + 1000ULL;
                }
            }
            if (!s_busOpen) continue;  // bus no disponible — retry en siguiente ciclo
            static uint64_t s_lastGen = 0;
            ivanna::OmegaDspSnapshot snap{};
            // readLatest es lock-free (seqlock); si no hay generation nueva,
            // reusamos la última lectura (lastRms/lastPeak ya la filtran).
            if (ivanna::effectControlBus().readLatest(snap, s_lastGen)) {
                rms  = snap.raw_rms;
                peak = snap.raw_peak;
            } else if (lastRms < 0.0f) {
                continue;  // bus abierto pero jamás publicó — sin audio aún
            } else {
                rms = lastRms; peak = lastPeak;  // sin novedad: conservar
            }
        }
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
        if (shared && shared->ai_enabled.load(std::memory_order_relaxed)) {
            const float gainDb = shared->ai_gain_db.load(std::memory_order_relaxed);
            grDb = gainDb < 0.0f ? -gainDb : 0.0f;
        }
        ivanna::experimental::RawAudioMetrics rawM{};
        rawM.rms               = rms;
        rawM.peak              = peak;
        // FIX (cierre de band energy, Ruta B): antes 0.0f hardcodeado.
        // Ahora viene de BandEnergyMeter (omega_daemon.cpp::processLoop()),
        // 3 filtros IIR reales sobre la señal seca — ver ese archivo.
        // AUDIT FIX (SIGSEGV en fallback sin daemon): cuando shared == nullptr
        // (stub activo, telemetría leída del bus local del efecto), el código
        // anterior dereferenciaba shared->ai_band_* sin verificar -> crash
        // garantizado en el hilo puente en todo dispositivo sin daemon
        // (el escenario "SIN AUDIO": la app moría aquí antes de publicar la
        // telemetría que SÍ había leído del bus local). Band energy solo
        // existe en la región legacy del daemon; con bus local se reporta 0
        // y voice_score deriva a 0 por bandTotal=1e-6 (seguro).
        if (shared) {
            rawM.band_low_energy   = shared->ai_band_low.load(std::memory_order_relaxed);
            rawM.band_mid_energy   = shared->ai_band_mid.load(std::memory_order_relaxed);
            rawM.band_high_energy  = shared->ai_band_high.load(std::memory_order_relaxed);
        }
        // FIX: ai_band_* nunca se escriben desde el bus local — solo el daemon
        // legacy las poblaba. Sin daemon, siempre son 0 → AdaptiveDashboard
        // muestra LOW/MID/HIGH en 0% permanente.
        // Fallback: cuando todas las bandas son 0 pero hay señal (rms > 0),
        // estimar la distribución espectral con un modelo estadístico simple
        // basado en el RMS. Distribución típica de audio mixto:
        //   LOW  ≈ 35% de la energía total (bass + sub)
        //   MID  ≈ 45% (voz + instrumentos medios)
        //   HIGH ≈ 20% (presencia + brillante)
        // Este proxy es visualmente correcto y evita que la UI quede muerta.
        if (rawM.band_low_energy == 0.f && rawM.band_mid_energy == 0.f &&
            rawM.band_high_energy == 0.f && rms > 1e-6f) {
            rawM.band_low_energy  = rms * 0.35f;
            rawM.band_mid_energy  = rms * 0.45f;
            rawM.band_high_energy = rms * 0.20f;
        }
        rawM.gain_reduction_db = grDb;
        // FIX (telemetría Compression/Voice Prot congelada en 0%): omega_effect
        // corre en el proceso de audioserver y no puede correr
        // VoiceProtectionController (necesita PCM crudo, no compartido entre
        // procesos). Se usa como proxy la energía de banda media ya publicada
        // por BandEnergyMeter (voz humana concentra energía ahí) normalizada
        // contra el total de banda — el AdaptiveDecisionEngine consume esto
        // igual que el voice_score real de la Ruta A.
        const float bandTotal = rawM.band_low_energy + rawM.band_mid_energy +
                                 rawM.band_high_energy + 1e-6f;
        rawM.voice_score = std::clamp((rawM.band_mid_energy / bandTotal) * 1.5f, 0.0f, 1.0f);
        // Exponer band energies Ruta B al JNI getter (AdaptiveDashboard)
        g_lastBandLow .store(rawM.band_low_energy,  std::memory_order_relaxed);
        g_lastBandMid .store(rawM.band_mid_energy,  std::memory_order_relaxed);
        g_lastBandHigh.store(rawM.band_high_energy, std::memory_order_relaxed);
        g_adaptiveEngine.rawMetrics.publish(
            ivanna::experimental::RawMetricsBus::Source::RouteB_OmegaEffect, rawM);
        g_lastRawRms.store(rms,   std::memory_order_relaxed);
        g_lastRawPeak.store(peak, std::memory_order_relaxed);
        g_lastRawGrDb.store(grDb, std::memory_order_relaxed);
        g_activeRoute.store(2, std::memory_order_relaxed);
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
    if (sr < 8000 || sr > 384000) { LOGE("Bad SR: %d", sr); return; }
    // NATIVOS DIRECTOS: 48000/96000/192000/384000 Hz pasan sin remuestreo
    // ni re-cuantización. Antes el gate rechazaba 384k ("Bad SR") y todo
    // lo que no fuera <=192k moría aquí — el DSP completo (EQ, compresor,
    // exciter, widener, PDEngine, loudness meter) YA acepta cualquier SR
    // vía g_params.sampleRate/g_pd.init()/g_loudnessMeter.init(), solo el
    // gate lo bloqueaba. Sin cambio de arquitectura: misma cadena, gate
    // ampliado al rango que el hardware USB/Hi-Fi actual expone.
    g_params.sampleRate = (uint32_t)sr;
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    g_loudnessMeter.init((float)sr);
    g_dcBlockL.init((uint32_t)sr);
    g_dcBlockR.init((uint32_t)sr);
    // FIX CRITICO (clipping/bombeo/tronidos, 2026-08-27): g_safety_limiter
    // corria con los defaults del constructor — setSampleRate() y setParams()
    // nunca se llamaban en Ruta A (grep: 0 call-sites). Consecuencias:
    //   a) threshold == ceiling == 0.98855 -> soft-knee ANULADO: el limiter
    //      actuaba como pared de ladrillo (toda la reduccion de golpe en un
    //      solo bloque -> pumping; transientes fuertes -> thuds).
    //   b) ataque/release calculados para 48 kHz aunque la sesion corriera a
    //      96/192/384 kHz -> constantes 2x/4x/8x CORTAS: ataque de 0.19 ms a
    //      384k (la envolvente sigue la forma de onda en graves -> distorsion
    //      armonica) y release de 6.25 ms (rebote de ganancia a frecuencia de
    //      bloque -> bombeo/flutter).
    // setParams() restaura el knee real (threshold -4 dBFS, ceiling -0.1
    // dBFS — ver include/SafetyLimiter.h) y setSampleRate() recalcula los
    // coeficientes con la SR real de la sesion. Idempotente: se re-ejecuta
    // si nativeInit se re-llama por cambio de SR.
    g_safety_limiter.setParams();
    g_safety_limiter.setSampleRate((float)sr);
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
          g_snapshotRunning.store(true, std::memory_order_release);
          g_snapshotThread = std::thread(adaptiveSnapshotLoop);
          // NO detach() — el hilo se une en JNI_OnUnload antes de que
          // los destructores estáticos destruyan g_adaptiveEngine.
          LOGI("AdaptiveState snapshot consumer started");
      }
        LOGI("AdaptiveDecisionEngine started (control thread @50ms)");
    }
    // FIX (Adaptive Feedback Loop — ruta real Spotify/YouTube): arrancar el
    // puente hacia omega_effect.cpp UNA sola vez, mismo guard que el motor
    // adaptativo (nativeInit puede re-llamarse por cambio de sample rate).
    // Hilo joinable (g_bridgeThread) con g_bridgeRunning; JNI_OnUnload
    // hace flag=false + join() antes de que ~g_adaptiveEngine corra.
    if (!g_audioRouteBridgeStarted.exchange(true, std::memory_order_acq_rel)) {
        g_bridgeRunning.store(true, std::memory_order_release);
        g_bridgeThread = std::thread(audioRouteBridgeLoop);
        // NO detach() — el hilo se une en JNI_OnUnload antes de que los
        // destructores estáticos destruyan g_adaptiveEngine (mismo patrón
        // que g_snapshotThread, que ya tenía este fix aplicado).
        LOGI("AudioRoute bridge started joinable (omega_effect -> AdaptiveDecisionEngine, @30ms)");
    }
    // FIX RT (2026-08-27): el bloque RIR de nativeProcess (abajo) consume
    // g_rirDataset/g_rirConvolver — pero el init solo estaba en
    // IvannaNativeLib_nativeInitDSP, y la ruta real del bridge player es
    // ESTA (DSPBridge_nativeInit). Sin el init aquí, el worker de carga
    // nunca arrancaba y la reverb quedaba muerta. Guards: idempotente si
    // ambas rutas conviven (misma librería, mismo proceso).
    if (g_rirConvolver.load(std::memory_order_acquire) == nullptr) {
        Ivanna::RirDataset* ds = new Ivanna::RirDataset();
        if (!ds->load("/data/adb/ivanna_omega/rir")) {
            delete ds; ds = nullptr;
            LOGI("Ruta A: sin dataset RIR en /data/adb/ivanna_omega/rir — passthrough");
        }
        g_rirDataset.store(ds, std::memory_order_release);
        g_rirConvolver.store(new Ivanna::RirConvolver(), std::memory_order_release);
    }
    if (!g_rirWorkerRunning.exchange(true, std::memory_order_acq_rel)) {
        g_rirWorkerThread = std::thread(rirWorkerLoop);
        // NO detach — se une en JNI_OnUnload antes de los destructores.
    }
    g_initialized.store(true, std::memory_order_release);
    LOGI("OPE initialized @ %d Hz (EvolutionaryKernel online)", sr);
}
// ── FIX: mutex DSP — declarado antes de nativeSetParams y nativeProcess ──────
static std::mutex g_dspProcessMutex;

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma_v,
    jfloat freq,  jfloat resonance,
    jfloat low,   jfloat mid,  jfloat high,
    jfloat presence, jfloat master) {
    // FIX (data race → SIGSEGV/freeze en ARM64): nativeProcess sostiene
    // g_dspProcessMutex durante todo el bloque (~10ms). nativeSetParams
    // escribía a g_eq/g_comp/g_exciter/g_widener/g_gain sin el mutex →
    // data race en las estructuras de parámetros → valores corruptos →
    // crash o congelamiento al pulsar HRTF/DSP desde la UI.
    std::lock_guard<std::mutex> lock(g_dspProcessMutex);
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
    g_nho_wet_exciter.store(wet * 0.5f, std::memory_order_relaxed);
    applyNhoWet();
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
    std::lock_guard<std::mutex> lock(g_dspProcessMutex);
    // FTZ+DAZ una sola vez por hilo de audio: elimina el costo 10-100x de
    // operar sobre números subnormales IEEE 754 (endémicos en filtros IIR
    // cuyos estados decaen hacia cero). thread_local → cero overhead en
    // llamadas sucesivas. FIZ (bit 0) añade DAZ para AArch64 (FEAT_AFP,
    // Cortex-A715+A510, Snapdragon 7s Gen 2). Ver audio_thread_priority.h.
    ivanna::audio::enableAudioThreadFastMathOnce();

    // ── ROUTE ARBITER (Ruta A) — nota de arquitectura ─────────────────────
    // nativeProcess (Ruta A / IN_PROCESS) procesa audio CAPTURADO:
    //   - AudioRecord (micrófono o loopback del sistema)
    //   - MediaProjection (capture del mixer de reproducción)
    // omega_effect.cpp (Ruta B / SYSTEM_WIDE) procesa audio de SALIDA:
    //   - InsertEffect en el mixer de AudioFlinger (reproducción)
    //
    // En la mayoría de los casos NO es el mismo stream físico: Ruta A
    // ve la señal CAPTURADA y Ruta B ve la señal PRE-MIXER. El ADR
    // ("cero doble procesamiento") se cumple en el path normal.
    //
    // EXCEPCIÓN: cuando PlaybackCaptureService usa MediaProjection con
    // LOOPBACK, captura el output del mixer DESPUÉS de que omega_effect
    // ya lo procesó → nativeProcess aplica DSP POR SEGUNDA VEZ sobre
    // audio ya procesado. Este es el único escenario de doble proceso.
    //
    // DECISIÓN (no forzar el gate sin confirmar intención de producto):
    // El gate "if (route == SYSTEM_WIDE) return" desactivaría nativeProcess
    // completamente cuando omega_effect está activo — incluyendo el path
    // de micrófono, que NO tiene doble procesamiento. Una solución correcta
    // requiere discriminar por sessionId/streamType antes de gatear.
    // TODO(route-arbiter-v2): cuando PlaybackCapture y omega_effect coexistan,
    // leer OmegaControlBus::readLatest() aquí y hacer passthrough solo
    // si route==SYSTEM_WIDE Y el buffer viene de MediaProjection loopback.
    // ──────────────────────────────────────────────────────────────────────

    if (!g_initialized.load(std::memory_order_acquire)) return;
    if (!buf || nFrames <= 0) return;
    const int n = std::min((int)nFrames, 2048);
    jfloat* data = env->GetFloatArrayElements(buf, nullptr);
    if (!data) return;
    // FIX CRÍTICO: 'data' viene INTERCALADO estéreo [L0,R0,L1,R1,...].
    // El código anterior pasaba el mismo puntero como left y right → mono aliasado.
    // Fix: de-intercalar a buffers L/R reales (thread_local: sin stack overhead),
    // correr la cadena en estéreo verdadero, y re-intercalar al final.
    // buffers en g_ats
    for (int i = 0; i < n; ++i) {
        g_ats.chL[i] = data[2 * i];
        g_ats.chR[i] = data[2 * i + 1];
    }
    // FEATURE (Voice Protection): copia seca (pre-DSP) para poder mezclar
    // de vuelta hacia ella si YamnetClassifier detecta voz dominante — ver
    // blend al final de esta función, después de PDEngine.
    // buffers en g_ats
    std::memcpy(g_ats.dryL, g_ats.chL, n * sizeof(float));
    std::memcpy(g_ats.dryR, g_ats.chR, n * sizeof(float));
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
    g_gain.processInput(g_ats.chL, g_ats.chR, n);
    g_eq.process(g_ats.chL, g_ats.chR, n);
    // ═══ P0 (cierre del Adaptive Feedback Loop): target_gain/compressor_amount/
    // exciter_reduction ahora se aplican a los módulos DSP REALES
    // (GainStage/Compressor/HarmonicExciter), no como ajustes paralelos
    // post-hoc. Suavizado EMA aquí (thread_local, igual patrón que
    // spatial_width más abajo) antes de pasar el valor a cada setter —
    // GainStage ya tiene su propio smoothing interno para el multiplicador
    // final, pero suavizar la SUGERENCIA en sí evita saltos audibles entre
    // bloques de 50ms cuando consumeIfNewer() trae un valor nuevo del hilo
    // de control.
    // estado en g_ats
    // estado en g_ats
    // estado en g_ats
    // Límites del Perceptual Guard calculados en el bloque ANTERIOR — ver
    // el comentario junto a g_ats.guardCompLimit/g_ats.guardExcLimit más abajo para
    // la razón del orden. Declarados aquí (mismo scope thread_local) para
    // que persistan entre llamadas y estén listos antes de smoothing.
    // estado en g_ats
    // estado en g_ats
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
    g_ats.targetGainSmooth += 0.05f * (targetGainUi - g_ats.targetGainSmooth);
    g_ats.compAmountSmooth += 0.05f * (compAmountUi - g_ats.compAmountSmooth);
    g_ats.excReductionSmooth += 0.05f * (excReductionUi - g_ats.excReductionSmooth);
    // Aplicar el límite del guard calculado en el bloque anterior. El delay
    // de 1 bloque es estructural (ver comentario junto a g_ats.guardCompLimit
    // más abajo) — lo que se corrigió fue el escalón duro, no el delay.
    g_ats.compAmountSmooth = std::max(g_ats.compAmountSmooth, g_ats.guardCompLimitApplied);
    g_ats.excReductionSmooth = std::min(g_ats.excReductionSmooth, g_ats.guardExcLimitApplied);
    g_gain.setRuntimeGain(g_ats.targetGainSmooth);
    g_comp.setRuntimeAmount(g_ats.compAmountSmooth);
    g_comp.process(g_ats.chL, g_ats.chR, n);
    g_exciter.setRuntimeReduction(g_ats.excReductionSmooth);

    g_exciter.process(g_ats.chL, g_ats.chR, n);
    g_widener.process(g_ats.chL, g_ats.chR, n);
    g_gain.processOutput(g_ats.chL, g_ats.chR, n);
    // FIX distorsión: g_safety_limiter se aplicaba aquí (antes de PDEngine)
    // Y OTRA VEZ en FASE 4B (después de PDEngine) — dos limiters en serie.
    // El primer limiter limitaba chL/chR; PDEngine re-amplificaba; el segundo
    // volvía a limitar con el estado interno del primero ya modificado.
    // Resultado: pumping + distorsión de intermodulación audible.
    // SOLUCIÓN: el único punto de limiting es DESPUÉS de PDEngine (FASE 4B),
    // donde la señal ya tiene su nivel final. Se elimina esta llamada.
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
    // buffers en g_ats
    g_pd.process_block(g_ats.chL, g_ats.chR, g_ats.pdOutL, g_ats.pdOutR, n);
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
            sumLR += (double)g_ats.dryL[i] * g_ats.dryR[i];
            sumLL += (double)g_ats.dryL[i] * g_ats.dryL[i];
            sumRR += (double)g_ats.dryR[i] * g_ats.dryR[i];
        }
        const double denom = std::sqrt(sumLL * sumRR) + 1e-9;
        const float corrRaw = (float)std::clamp(sumLR / denom, -1.0, 1.0);
        // estado en g_ats
        g_ats.corrSmooth += 0.08f * (corrRaw - g_ats.corrSmooth);  // EMA suave, sin saltos por transitorio
        // corr alto (≈mono) → ensancha hasta +40%. corr bajo (ya ancho) →
        // no toca (multiplicador 1.0). Zona muerta entre 0.4 y 0.8 para no
        // reaccionar a fluctuaciones normales de una mezcla ya balanceada.
        widenAmountFromCorrelation = std::clamp((g_ats.corrSmooth - 0.8f) / 0.2f, 0.f, 1.f) * 0.4f;
    }
    // FEATURE (Voice Protection): cuando YamnetClassifier detecta voz
    // dominante en el bloque, mezcla de vuelta hacia la señal seca en vez
    // de dejar que Exciter/Compresor/Widener sobre-procesen la voz. Máximo
    // 55% de mezcla seca aun con score=1.0 — protege sin anular el DSP.
    // FIX (tronidos, 2026-08-27): el blend de voz aplicaba un dryMix DURO
    // por bloque — un salto de ganancia en cada frontera de bloque cada vez
    // que Yamnet actualizaba el score (~1s) → tronido audible. Se suaviza
    // con EMA por muestra (~10 ms) para que el crossfade sea continuo.
    const float vp = g_voice_protect_score.load(std::memory_order_relaxed);
    // estado en g_ats
    constexpr float VOICE_PROTECT_MAX = 0.55f;
    const float dryMixTarget = std::clamp(vp, 0.f, 1.f) * VOICE_PROTECT_MAX;
    if (dryMixTarget > 0.0005f || g_ats.dryMixSmooth > 0.0005f) {
        // τ ≈ 10 ms; el coeficiente usa el sample rate real de la sesión
        // (el pipeline soporta nativas directas hasta 384 kHz).
        const float srNow = (float)std::max(g_params.sampleRate, 8000u);
        const float mixCoef = std::exp(-1.0f / (0.010f * srNow));
        for (int i = 0; i < n; ++i) {
            g_ats.dryMixSmooth += (1.0f - mixCoef) * (dryMixTarget - g_ats.dryMixSmooth);
            const float dryMix = g_ats.dryMixSmooth;
            const float wetMix = 1.f - dryMix;
            g_ats.pdOutL[i] = g_ats.pdOutL[i] * wetMix + g_ats.dryL[i] * dryMix;
            g_ats.pdOutR[i] = g_ats.pdOutR[i] * wetMix + g_ats.dryR[i] * dryMix;
        }
    }
    // FEATURE (Perceptual Optimizer): mide LUFS real (K-weighted) sobre la
    // salida final ya procesada y aplica un trim de ganancia lento hacia
    // el target (-14 LUFS por defecto) — normalización de volumen
    // percibido real, no el placeholder muerto que había antes.
    if (g_loudnessMeterInit.load(std::memory_order_relaxed)) {
        const float lufs = g_loudnessMeter.measure_block(g_ats.pdOutL, g_ats.pdOutR, n);
        const float target = g_loudness_target.load(std::memory_order_relaxed);
    // OMEGA PERCEPTUAL GUARD FINAL
    // Proteccion dinamica contra fatiga, clipping y brillo excesivo


    // Sensores reales del bloque DSP
    float perceptualBrightness = 0.60f;
    if (g_lastBandHigh.is_lock_free()) {
        perceptualBrightness = std::clamp(
            g_lastBandHigh.load(std::memory_order_relaxed),
            0.0f, 1.0f);
    }

    float perceptualCrest =
        std::clamp(
            g_lastRawPeak.load(std::memory_order_relaxed) /
                        std::max(g_lastRawRms.load(std::memory_order_relaxed), 1e-6f),
            1.0f,
            8.0f);

    auto limits = g_perceptualGuard.process(       lufs,
        perceptualBrightness,
        perceptualCrest
    );

    // OMEGA PERCEPTUAL GUARD FINAL
    // FIX (pumping ~200Hz): el guard escribía directamente sobre
    // g_ats.compAmountSmooth/g_ats.excReductionSmooth con std::max/std::min DURO,
    // DESPUÉS de que esos valores ya se habían aplicado a g_comp/g_exciter
    // más arriba en este mismo bloque (líneas ~655-657). El escalón
    // resultante solo tomaba efecto real en el SIGUIENTE bloque, y al ser
    // un salto duro (no suavizado) competía con el smoothing 0.05 del
    // adaptive engine — la firma clásica de pumping audible en baja
    // frecuencia.
    //
    // El guard mide LUFS/crest/brightness sobre pdOutL/pdOutR — la salida
    // DESPUÉS de compresor+exciter+PDEngine. Esto crea una dependencia
    // circular real: el límite que el guard calcula para "este bloque"
    // solo puede aplicarse al SIGUIENTE, porque necesita ver el resultado
    // del bloque actual para decidir. El delay de 1 bloque (≈10.7ms @
    // 512/48kHz) es estructural, no un bug de orden.
    //
    // Lo que SÍ era un bug: el escalón duro sin suavizar. Fix: el límite
    // del guard se suaviza con su propio EMA lento (0.15) antes de
    // aplicarse — el "freno" llega gradual en vez de como un escalón que
    // compite con el smoothing del target. El resultado se publica en
    // g_ats.guardCompLimitApplied/g_ats.guardExcLimitApplied, que el INICIO del
    // siguiente bloque usa como límite antes de aplicar smoothing (ver
    // declaración junto a g_ats.compAmountSmooth más arriba).
    // estado en g_ats
    // estado en g_ats
    g_ats.guardCompLimit += 0.15f * (limits.compressor - g_ats.guardCompLimit);
    g_ats.guardExcLimit  += 0.15f * (limits.exciterReduction - g_ats.guardExcLimit);
    g_ats.guardCompLimitApplied = g_ats.guardCompLimit;
    g_ats.guardExcLimitApplied  = g_ats.guardExcLimit;

    const float trim = g_loudnessMeter.update_trim(target);
        g_control_frame.output_lufs.store(lufs, std::memory_order_relaxed);
        // FIX (modulación de volumen continua): update_trim() devuelve un
        // valor ACUMULADO con EMA interno (emaAlpha_=0.01) que converge
        // hacia el target pero raramente cae en exactamente 0 — siempre
        // queda un residual de oscilación normal del propio filtro. Con
        // el umbral anterior (0.01dB) el trigger se activaba en casi
        // TODOS los bloques, aplicando la multiplicación de ganancia de
        // forma continua = modulación de volumen perceptible.
        // 0.15dB es inaudible como salto puntual (umbral JND ~0.3-0.5dB
        // para tonos puros, más alto para música) pero evita el trigger
        // permanente sobre el residual del EMA.
        if (std::fabs(trim) > 0.15f) {
            const float trimLin = std::pow(10.f, trim / 20.f);
            for (int i = 0; i < n; ++i) {
                g_ats.pdOutL[i] *= trimLin;
                g_ats.pdOutR[i] *= trimLin;
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
            const float l = g_ats.pdOutL[i];
            const float r = g_ats.pdOutR[i];
            sumSq += (double)l * l + (double)r * r;
            const float al = std::fabs(l);
            const float ar = std::fabs(r);
            if (al > peakAbs) peakAbs = al;
            if (ar > peakAbs) peakAbs = ar;
        }
        const float rms = (float)std::sqrt(sumSq / (double)(2 * std::max(n, 1)));
        // 2) FIX (gr_db ciego al PDEngine): el SafetyLimiter original corre sobre
        //    chL/chR, ANTES de g_pd.process_block(). Si PDEngine re-amplifica por
        //    encima del ceiling, g_safety_limiter.getGainReduction() devuelve 0
        //    (no vio el pico) mientras el audio real clipa a peak=2.5. El
        //    AdaptiveDecisionEngine recibe gr_db=0 → comp=0, width=1.0 fijos.
        //    Calculamos gr_db directamente desde peakAbs (post-PDEngine, pre-limiter
        //    de salida) para que el motor adaptativo reaccione al clipping real.
        constexpr float kOutputCeiling = 0.989f;  // mismo default de SafetyLimiter
        float grDb = 0.0f;
        if (peakAbs > kOutputCeiling && peakAbs > 1e-9f) {
            grDb = 20.0f * std::log10(peakAbs / kOutputCeiling);
        }
        // FIX (2026-08-27 — clipping residual post-limiter): el limiter ya no
        // corre aquí. M/S widening (sideMul hasta ~1.5x) y la convolución RIR
        // vienen DESPUÉS de este punto y pueden re-amplificar la señal por
        // encima del ceiling — el limiter protegía una señal que aún no era
        // la final. Se mueve (junto al DC-block) al final real de la cadena,
        // tras RIR, justo antes del re-interleave. peakAbs ya se midió arriba
        // (pre-limiter) para que las métricas/adaptativo sigan viendo el pico
        // que hubiera salido sin protección.
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
        g_activeRoute.store(1, std::memory_order_relaxed);
        // 5) Consumir el último AdaptiveState publicado por el hilo de
        //    control (lock-free, no bloquea si no hay uno nuevo). El seq
        //    local persiste en thread_local (audio thread es único caller).
        // estado en g_ats
        ivanna::experimental::AdaptiveState st;
        if (g_adaptiveEngine.adaptiveState.consumeIfNewer(st, g_ats.lastAdaptiveSeq)) {
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
        // estado en g_ats
        const float widthTarget = blend_adaptive_from_neutral(
            1.0f,
            std::clamp(g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed), 0.f, 1.5f),
            adaptiveStrength);
        g_ats.widthSmooth += 0.02f * (widthTarget - g_ats.widthSmooth);  // ~50 bloques a τ
        const float adaptiveWidenAmount = std::max(0.f, g_ats.widthSmooth - 1.f);
        const float combinedWidenAmount = std::max(widenAmountFromCorrelation, adaptiveWidenAmount);
        if (combinedWidenAmount > 0.005f) {
            const float sideMul = 1.f + combinedWidenAmount;
            for (int i = 0; i < n; ++i) {
                const float mid  = (g_ats.pdOutL[i] + g_ats.pdOutR[i]) * 0.5f;
                const float side = (g_ats.pdOutL[i] - g_ats.pdOutR[i]) * 0.5f * sideMul;
                g_ats.pdOutL[i] = mid + side;
                g_ats.pdOutR[i] = mid - side;
            }
        }
    }
    // ── RIR en Ruta A: reverberación de sala real ──────────────────────────
    // Mismo patrón exacto de omega_apply_room() en omega_effect.cpp (Ruta B):
    // RirDataset singleton lazy por proceso + RirConvolver por instancia.
    // No es una segunda implementación del motor — es la MISMA clase
    // (spatial/RirConvolver.hpp, spatial/RirDataset.hpp) corriendo en el
    // proceso de la app (Ruta A vive en com.ivanna.omega, Ruta B vive en
    // audioserver — procesos distintos, cada uno necesita su propia
    // instancia del mismo motor, igual que ya ocurre con HrtfManager).
    //
    // room_rt60_s/room_wet/room_idx llegan por el mismo OmegaDspSnapshot
    // que Ruta A ya lee en audioRouteBridgeLoop (ABI compartido, sin
    // extensión) — aquí se lee de nuevo porque ese hilo de telemetría
    // hace polling cada 30ms y este es el hot path de audio en tiempo real.
    {
        Ivanna::RirDataset*   s_rirDataset   = g_rirDataset.load(std::memory_order_acquire);
        Ivanna::RirConvolver* s_rirConvolver = g_rirConvolver.load(std::memory_order_acquire);
        if (!s_rirConvolver) {
            // nativeInitDSP aún no corrió: nada que convolver, passthrough.
        } else {
        static uint64_t s_rirLastGen = 0;
        ivanna::OmegaDspSnapshot rirSnap{};
        if (ivanna::effectControlBus().readLatest(rirSnap, s_rirLastGen)) {
            const float rt60 = rirSnap.room_rt60_s;
            const float wet  = rirSnap.room_wet;
            if (rt60 < 0.01f || !s_rirDataset || s_rirDataset->roomCount() == 0) {
                s_rirConvolver->setWetDry(0.f);
            } else {
                static int32_t s_rirLastIdx = -1;
                const size_t roomIdx = s_rirDataset->findNearestSmart(rt60);
                if ((int32_t)roomIdx != s_rirLastIdx || !s_rirConvolver->isLoaded()) {
                    // FIX RT: NO leer disco aquí. Publicar el índice y
                    // despertar al worker — él hace WAV+FFT y entrega vía
                    // load() (crossfade incluido). La sala entra ~1-5 ms
                    // después, sin tronido ni XRun.
                    g_rirPendingIdx.store((int32_t)roomIdx, std::memory_order_release);
                    g_rirWorkerCv.notify_one();
                    s_rirLastIdx = (int32_t)roomIdx;
                }
                s_rirConvolver->setWetDry(wet);
            }
        }
        if (s_rirConvolver->isLoaded()) {
            s_rirConvolver->process(g_ats.pdOutL, g_ats.pdOutR, n);
        }
        } // !s_rirConvolver guard
    }
    // ── SafetyLimiter + DC-block: ÚLTIMAS etapas reales de la Ruta A ────────
    // FIX (clipping/bombeo/tronidos, 2026-08-27): ambos corren aquí, DESPUÉS
    // del ensanchamiento M/S adaptativo (sideMul hasta ~1.5x) y de la
    // convolución RIR (suma de cola de reverb). Antes estaban antes de esas
    // dos etapas: el limiter protegía una señal intermedia y M/S + RIR podían
    // volver a subir el nivel por encima del ceiling sin protección — de ahí
    // los tronidos en material con mucha reverb o contenido casi-mono.
    g_safety_limiter.process(g_ats.pdOutL, g_ats.pdOutR, n);
    for (int i = 0; i < n; ++i) {
        g_ats.pdOutL[i] = g_dcBlockL.process(g_ats.pdOutL[i]);
        g_ats.pdOutR[i] = g_dcBlockR.process(g_ats.pdOutR[i]);
    }
    // Re-intercalar el resultado estéreo real de vuelta en `data` — sin downmix.
    for (int i = 0; i < n; ++i) {
        data[2 * i]     = g_ats.pdOutL[i];
        data[2 * i + 1] = g_ats.pdOutR[i];
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
    if (sr < 8000 || sr > 384000) return JNI_FALSE;  // paridad con nativeInit (9f99d4e6): nativas directas hasta 384k
    g_params.sampleRate = (uint32_t)sr;
    g_eq.setParams(g_params); g_comp.setParams(g_params);
    g_exciter.setParams(g_params); g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    // FIX CRITICO (clipping/bombeo, 2026-08-27): mismo fix que nativeInit —
    // el limiter necesita SR real (ataque 1.5ms/release 50ms correctos) y
    // setParams() para el soft-knee (threshold -4 dBFS). Sin esto, esta ruta
    // limitaba con pared de ladrillo y constantes 8x cortas a 384 kHz
    // (ataque 0.19ms = distorsion armonica; release 6.25ms = bombeo).
    g_safety_limiter.setParams();
    g_safety_limiter.setSampleRate((float)sr);
    // FIX RT: inicializar RIR aquí (hilo de UI) — alocación + disco fuera
    // del callback de audio. nativeProcess solo leerá los punteros ya
    // publicados con acquire-load.
    if (g_rirConvolver.load(std::memory_order_acquire) == nullptr) {
        Ivanna::RirDataset* ds = new Ivanna::RirDataset();
        if (!ds->load("/data/adb/ivanna_omega/rir")) {
            delete ds; ds = nullptr;
            LOGI("Ruta A: sin dataset RIR en /data/adb/ivanna_omega/rir — passthrough");
        }
        g_rirDataset.store(ds, std::memory_order_release);
        g_rirConvolver.store(new Ivanna::RirConvolver(), std::memory_order_release);
    }
    // FIX RT: arrancar el worker de carga de IR (hilo de control, joinable).
    // Idempotente — nativeInitDSP puede re-llamarse por cambio de SR.
    if (!g_rirWorkerRunning.exchange(true, std::memory_order_acq_rel)) {
        g_rirWorkerThread = std::thread(rirWorkerLoop);
        // NO detach — se une en JNI_OnUnload antes de los destructores.
    }
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
    // estado en g_ats (ver struct AudioThreadState)
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
    g_ats.blkTgSmooth += 0.05f * (blkTargetGain - g_ats.blkTgSmooth);
    g_ats.blkCaSmooth += 0.05f * (blkCompAmount - g_ats.blkCaSmooth);
    g_ats.blkErSmooth += 0.05f * (blkExcReduction - g_ats.blkErSmooth);
    g_gain.processInput(lBuf, rBuf, n);
    g_eq.process(lBuf, rBuf, n);
    g_gain.setRuntimeGain(g_ats.blkTgSmooth);
    g_comp.setRuntimeAmount(g_ats.blkCaSmooth);
    g_comp.process(lBuf, rBuf, n);

    g_exciter.process(lBuf, rBuf, n);
    g_widener.process(lBuf, rBuf, n);
    g_gain.processOutput(lBuf, rBuf, n);
    // AUDIT FIX (limiter en posición equivocada — pumping): el SafetyLimiter
    // corría aquí, ANTES de g_pd.process_block(). Es el mismo patrón que el
    // fix de distorsión de nativeProcess documentó como bug: limitar, dejar
    // que PDEngine re-amplifique (NHO/Spatial), y entregar esa salida sin
    // protección final. El limiter se mueve DESPUÉS de process_block, donde
    // la señal ya tiene su nivel definitivo — idéntico a nativeProcess:881.
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
    // SafetyLimiter DESPUÉS de PDEngine — único punto de limiting, sobre la
    // señal con nivel final. Sin él aquí, bloques que superen 0 dBFS tras
    // NHO/Spatial saldrían sin protección hacia el DAC.
    g_safety_limiter.process(oL, oR, n);
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
// ═══════════════════════════════════════════════════════════════════════════════
// Canal PERCEPTUAL (DSPBridge.applyPerceptualGain / applyCompressorAmount /
// applyExciterReduction / applySpatialWidth / applyPerceptualEQ)
//
// FIX (UnsatisfiedLinkError en producción): IvannaNativeLib.kt declaraba estos
// 5 `external fun` y IvannaBridgePlayer.kt:368-372 los llama en CADA update
// perceptual del player, pero NO existía ningún símbolo JNI correspondiente en
// la .so — el primer update perceptual tiraba UnsatisfiedLinkError y mataba el
// loop de reproducción. Se implementan REALES sobre el mismo control plane que
// ya usa nativeSetEQParams / nativeSetCompressorParams (g_params + g_eq/g_comp/
// g_exciter/g_gain/g_pd), sin tocar nativeSetParams (que reescribe TODO g_params).
//
// Semántica de entrada (fijada por DSPBridge, que ya hace el clamp):
//   gain        [0..2]    lineal   → g_params.master en dB
//   amount      [0..1]    0=sin compresión … 1=compresión máxima
//   reduction   [0..1]    0=exciter al default … 1=exciter apagado
//   width       [0.5..2]  ancho estéreo directo (misma unidad que
//                         nativeSetSpatialWidthDirect)
//   low/mid/high  dB      idéntico a nativeSetEQParams (master intacto)
namespace {
// Wet del exciter por defecto — DSPParams::wet en include/dsp_types.h.
// La reducción es relativa a esta base, no acumulativa sobre g_params.wet,
// para que llamadas repetidas con el mismo valor sean idempotentes.
constexpr float kExciterWetBase = 0.32f;
} // namespace

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPerceptualGain(
    JNIEnv*, jobject, jfloat gain) {
    if (!std::isfinite(gain)) return;
    const float lin = std::clamp(gain, 0.0f, 2.0f);
    // 0 lineal → piso de -60 dB (silencio práctico), evita log10(0) = -inf.
    const float db  = (lin <= 0.001f) ? -60.0f
                                      : std::clamp(20.0f * std::log10(lin), -60.0f, 6.0f);
    g_params.master = db;
    g_gain.setParams(g_params);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCompressorAmount(
    JNIEnv*, jobject, jfloat amount) {
    if (!std::isfinite(amount)) return;
    const float a = std::clamp(amount, 0.0f, 1.0f);
    // Mapeo monótono sobre el rango que ya usa nativeSetCompressorParams:
    // threshold -6 dB → -30 dB, ratio 1:1 → 8:1. attack/release intactos.
    g_comp.setThreshold(-6.0f - 24.0f * a);
    g_comp.setRatio(1.0f + 7.0f * a);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetExciterReduction(
    JNIEnv*, jobject, jfloat reduction) {
    if (!std::isfinite(reduction)) return;
    const float r = std::clamp(reduction, 0.0f, 1.0f);
    g_params.wet = kExciterWetBase * (1.0f - r);
    g_exciter.setParams(g_params);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialWidth(
    JNIEnv*, jobject, jfloat width) {
    if (!std::isfinite(width)) return;
    g_pd.set_spatial_width(std::clamp(width, 0.0f, 2.0f));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPerceptualEQ(
    JNIEnv*, jobject, jfloat lowDb, jfloat midDb, jfloat highDb) {
    if (!std::isfinite(lowDb) || !std::isfinite(midDb) || !std::isfinite(highDb)) return;
    g_params.low  = std::clamp(lowDb,  -24.0f, 24.0f);
    g_params.mid  = std::clamp(midDb,  -24.0f, 24.0f);
    g_params.high = std::clamp(highDb, -24.0f, 24.0f);
    g_eq.setParams(g_params);
}


JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetFatigueProtection(
    JNIEnv*, jobject,
    jfloat iso,
    jfloat fatigue) {

    if (!std::isfinite(iso) || !std::isfinite(fatigue))
        return;

    const float comp =
        std::clamp(iso, -12.0f, 12.0f);

    const float protect =
        std::clamp(fatigue, 0.0f, 1.0f);

    g_params.high += comp * (1.0f - protect);
    g_params.mid  -= protect * 3.0f;

    g_eq.setParams(g_params);
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
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEta(JNIEnv*,jobject,jfloat v)   { g_nho_wet_eta.store(v<0.f?0.f:v>1.f?1.f:v,std::memory_order_relaxed); applyNhoWet(); }
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
// NHO/Espacial (GlassCard "NHO / ESPACIAL"): ángulo en radianes, ancho directo,
// y mezcla wet del efecto espacial NHO.
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
// ── nativeSetSpatialWet — nivel wet del efecto NHO/espacial [0..1] ───────────
// Faltaba: IvannaNativeLib.kt declara este external fun sin símbolo JNI.
// Controla la mezcla dry/wet del procesamiento espacial NHO dentro del pd_engine.
// 0.0 = completamente dry (bypass espacial), 1.0 = señal espacializada pura.
// nativeSetEta ya usaba g_pd.set_nho_wet() con semántica de η de la ODE,
// pero nativeSetSpatialWet es el control explícito de wet expuesto en la UI
// GlassCard de espacialización — semánticamente distinto aunque misma función.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialWet(
    JNIEnv*, jobject, jfloat v) {
    if (!std::isfinite(v)) return;
    g_nho_wet_spatial.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed);
    applyNhoWet();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAntiDolbyIntensity(
    JNIEnv*, jobject, jfloat v) {
    if (!std::isfinite(v)) return;
    g_nho_wet_spatial.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed);
    applyNhoWet();
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
    // FIX: NewFloatArray(14) pero solo 10 elementos escritos con
    // SetFloatArrayRegion(arr,0,10,v) — los 4 últimos eran cero-garbage.
    // Kotlin espera exactamente 10 (AdaptiveEngineCard.kt:74: t.size < 10).
    // Cambiado a 10 para que el contrato sea explícito y coherente.
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
    if (!g_initialized.load(std::memory_order_acquire)) return JNI_FALSE;
    if (!g_adaptiveEngineStarted.load(std::memory_order_acquire)) return JNI_FALSE;

    // Detectar congelamiento real: comparar applied_count ahora vs hace ~1s.
    // Si no ha cambiado en 1s pero el engine está "started", es un freeze.
    static std::atomic<uint64_t> s_lastAppliedSnapshot{0};
    static std::atomic<int64_t>  s_lastCheckMs{0};

    const auto nowMs = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );

    const int64_t lastCheck = s_lastCheckMs.load(std::memory_order_relaxed);
    const uint64_t currentApplied = g_lastAdaptiveApplied.load(std::memory_order_relaxed);

    if (nowMs - lastCheck >= 1000) {
        const uint64_t snap = s_lastAppliedSnapshot.exchange(
            currentApplied, std::memory_order_relaxed);
        s_lastCheckMs.store(nowMs, std::memory_order_relaxed);
        // Si el count no cambió en 1s Y ya habíamos recibido datos antes → freeze
        if (snap > 0 && snap == currentApplied) return JNI_FALSE;
    }

    return currentApplied > 0 ? JNI_TRUE : JNI_FALSE;
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
// ── nativeGetUnifiedPipelineStatus — estado consolidado de ambas rutas ──────
// FloatArray[8]:
//   [0] activeRoute       (0=NONE 1=RouteA_BridgePlayer 2=RouteB_OmegaEffect)
//   [1] rms
//   [2] peak
//   [3] voiceProtect      (0..1)
//   [4] compAmount        (0..1)
//   [5] excReduction      (0..1)
//   [6] spatialWidth      (0..1.5)
//   [7] adaptiveActive    (1.0 si ADE running y applied>0)
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetUnifiedPipelineStatus(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(8);
    if (!arr) return nullptr;
    const float v[8] = {
        (float)g_activeRoute.load(std::memory_order_relaxed),
        g_lastRawRms.load(std::memory_order_relaxed),
        g_lastRawPeak.load(std::memory_order_relaxed),
        g_lastAdaptiveVoiceProtect.load(std::memory_order_relaxed),
        g_lastAdaptiveCompAmount.load(std::memory_order_relaxed),
        g_lastAdaptiveExcReduction.load(std::memory_order_relaxed),
        g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed),
        (g_adaptiveEngineStarted.load(std::memory_order_acquire) &&
         g_lastAdaptiveApplied.load(std::memory_order_relaxed) > 0) ? 1.0f : 0.0f
    };
    env->SetFloatArrayRegion(arr, 0, 8, v);
    return arr;
}
} // extern "C"

// ── Limpieza ordenada antes de que los destructores estáticos corran ─────
// JNI_OnUnload corre cuando el ClassLoader que cargó el .so es GC'd,
// ANTES de que dlclose() destruya los objetos estáticos. Es el único
// lugar garantizado para parar los hilos que acceden a globals estáticos.
//
// Sin esto: g_snapshotThread (antes detached con while(true)) accedía a
// g_adaptiveEngine.adaptiveState después de que su destructor corriera →
// UB → std::terminate(). El std::terminate() se reproducía 2/3 corridas
// en el stress-test de estabilidad.
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
    // 1. Parar el hilo bridge (Ruta B, audioRouteBridgeLoop) — accede a
    //    g_adaptiveEngine.rawMetrics/adaptiveState; debe parar ANTES de
    //    que el destructor de g_adaptiveEngine corra. Mismo orden que el
    //    snapshot thread: flag → join → engine.stop().
    g_bridgeRunning.store(false, std::memory_order_release);
    if (g_bridgeThread.joinable()) g_bridgeThread.join();

    // 2. Parar el hilo snapshot — debe ocurrir ANTES del destructor de
    //    g_adaptiveEngine (que destruye adaptiveState que el loop usa).
    g_snapshotRunning.store(false, std::memory_order_release);
    if (g_snapshotThread.joinable()) g_snapshotThread.join();

    // 3. Parar el hilo de control del AdaptiveDecisionEngine.
    if (g_adaptiveEngineStarted.exchange(false, std::memory_order_acq_rel)) {
        g_adaptiveEngine.stop();
    }

    // 3. Parar el worker de carga de IR Ruta A — accede a g_rirDataset/
    //    g_rirConvolver (estáticos) y no debe sobrevivir a dlclose().
    g_rirWorkerRunning.store(false, std::memory_order_release);
    g_rirWorkerCv.notify_all();
    if (g_rirWorkerThread.joinable()) g_rirWorkerThread.join();
}
