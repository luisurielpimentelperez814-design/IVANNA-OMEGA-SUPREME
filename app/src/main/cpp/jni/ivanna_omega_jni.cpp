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
#include "../EvolutionaryEQ.hpp"
#include "omega_perceptual_guard.h"
#include "../include/audio_thread_priority.h"
#include "../include/omega_control_bus.h"   // effectControlBus(), OmegaDspSnapshot, OMEGA_EFFECT_LOCAL_BUS_PATH
#include "../include/omega_effect_beacon.h"   // readEffectBeacon(): senal real de que omega_effect procesa
#include <android/log.h>
#include <cstring>
#include <string>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <thread>
#include <pthread.h>
#include <mutex>
#include <chrono>
#include <condition_variable>
// ── Puente de control app→daemon (Ruta B cross-process) ──────────────────
// El estado de Upmixing escrito desde la UI vive en el PROCESO APP; el DSP
// system-wide corre en el PROCESO AUDIOSERVER (libomega_effect.so). Son
// address-spaces separados: un std::atomic aqui jamas lo ve el efecto.
// El canal oficial es: JNI -> socket @omega_command_socket -> daemon
// (command_server) -> OmegaControlBus (SHM seqlock) -> omega_effect.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <cstddef>
#include <vector>  // nativeLoadVolterraKernelPackage (Genezi v2.5.369)
#include "../include/dsp_types.h"
#include "../include/ParametricEQ.h"
#include "../include/Compressor.h"
#include "../include/HarmonicExciter.h"
#include "../include/StereoWidener.h"
#include "../dsp/HeadphoneCrossfeed.hpp"
#include "../spatial/HolosOperator.hpp"
#include "../spatial/OrbeBinaural.hpp"
#include "../spatial/OmniField.hpp"  // IVANNA (v2.5.375): operador unificado de campo espacial
#include "../include/GainStage.h"
#include "../include/SafetyLimiter.h"
#include "../include/differential_reinjector.hpp"   // Ruta A: reinyeccion diferencial (v2.5.0)
#include "../include/acoustic_unity_engine.hpp"      // Ruta A: coordinacion acustica medida (v2.5.15)
#include "../spatial/RoomProbe.hpp"
#include "../spatial/RealAcousticSensor.hpp"   // sensor acústico real por micrófono (v2.5.369, Ruta A)
#include "../spatial/HyperStateOperator.hpp"  // Hiper-Operador de Estado Φ (v2.5.369, Ruta A)
#include "../spatial/RoomCorrectionEq.hpp"     // corrección de sala en vivo por el sensor (v2.5.369, Ruta A)
#include "../spatial/SceneGeometryRenderer.hpp"
#include "../spatial/RouteAWfsEngine.hpp"
#include "../neuromorphic/NpeStereoCore.hpp"
#include "../neuromorphic/neuro_acoustic_closed_loop.hpp"
#include "../dsp/NeuromorphicAbHarness.hpp"                // corrección acústica de sala: detección síncrona y diseño (2.5.49)
#include "../dsp/dsp_load_shed_governor.hpp"              // shed de carga DSP por deuda (sobrecarga Ruta A, v2.5.375)
#include "../include/BlockPeakGuard.h"
#include "../include/omega_user_flags.hpp"            // app -> daemon -> snapshot: bits de usuario para Ruta B (v2.5.16)
#include "../include/punch_engine.hpp"                // impacto predictivo por bandas (v2.5.41)
#include "../spatial/RirConvolver.hpp"
#include "../spatial/RirDataset.hpp"
#include "../spatial/SofaSafRirMasterKnowledge.hpp"
#include "../spatial/IvannaAudioPipeline.hpp"
#include "../music_intelligence/ImeBridge.hpp"
#include "../music_intelligence/FourDControlSurface.hpp"   // Cadena 4D: alimentación, diafonía y dosímetro (v2.5.48)
#include "../spatial/RoomGeometryConfig.hpp"
#include "../spatial/CrosstalkCanceller.hpp"
#include "../spatial/AuxGeneziRoute.hpp"
#include "../pd_engine.hpp"
#include "../control_frame.hpp"
#include "../include/dc_blocker.hpp"
#include "../audio_control_plane.hpp"
#include "../equal_loudness.hpp"
#include "../experimental/adaptive_engine/adaptive_decision_engine.hpp"
#include "../perceptual_loudness.hpp"
#include "../ivannalab/ivannalab.h"
#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../hexagon/ivanna_fastrpc_client.hpp"
#include "../supreme/SupremeAcousticStabilityGuard.hpp"
#include "../supreme/SpatialAngleReadback.hpp"
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
// omega_daemon_bridge.cpp DEFINE este simbolo como `extern "C"`,
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
// Crossfeed de graves con ITD (audifonos/DAC), Ruta A. strength 0 = apagado = bypass bit-exacto (default).
static ivanna::HeadphoneCrossfeed g_crossfeed;
// ORBE (Binaural Esférico de Campo Cercano), Ruta A: instancia propia del proceso app.
// amount=0 = bypass bit-exacto. ACTIVO por defecto (amount=1.0, directiva
// explícita 2026-11-08; la puerta GREEN quedó anulada). Se prepara en
// nativeInit/nativeInitDSP y se procesa en nativeProcess tras el crossfeed,
// EN SERIE antes de HOLOS (ORBE → HOLOS → IVANNA → Φ). La Ruta B usa su
// propia instancia (IvannaFusionEngine::m_orbe) en el proceso audioserver.
static ivanna::OrbeBinaural g_orbeRouteA;
// HOLOS (Operador Único de Campo Acústico), Ruta A: instancia propia del proceso app.
// amount=0 = bypass bit-exacto. ACTIVO por defecto (amount=0.5, harmonics=0.0
// retuneado, directiva explícita 2026-11-08; la puerta GREEN quedó anulada).
// Se prepara en nativeInit/nativeInitDSP y se procesa en nativeProcess tras
// ORBE, EN SERIE (ORBE → HOLOS → IVANNA → Φ): con ambos activos ORBE
// TAMBIÉN corre — el if/else que hacía a HOLOS sustituto de ORBE se eliminó
// en v2.5.369. La Ruta B usa su propia instancia (IvannaFusionEngine::m_holos)
// en el proceso audioserver.
static ivanna::HolosOperator g_holosRouteA;
// IVANNA (Operador Unificado de Campo Espacial, v2.5.375), Ruta A:
// instancia propia del proceso app. BYPASS por defecto desde v2.5.410
// (g_omni_amount=0.0; antes 1.0): medición host de la 410, residuo
// no-lineal −3.7/−6.1 dB en pseudo-música estéreo, fundamental colapsada.
// Corre en serie tras HOLOS y antes de Φ: ORBE → HOLOS → IVANNA → Φ.
// ORBE y HOLOS siguen activos (directiva 2026-11-08); IVANNA re-espacializa
// el campo ya mejorado por ellos. amount=0 = bypass bit-exacto.
static ivanna::omnifield::OmniField g_omniRouteA;
// IVANNA — telemetría de CPU Ruta A (v2.5.380): muestreo 1/64 bloques del
// tiempo de g_omniRouteA.process() (µs, CLOCK_MONOTONIC vía
// monotonicNowUs(), vDSO). Solo fetch_add en el hilo de audio: sin locks,
// sin log, sin allocations. La media vive en nativeGetOmniState() y en el
// log con límite de frecuencia del hilo de control. g_omniSampleTick
// cuenta TODAS las llamadas (el muestreo es (tick & 63) == 0).
static std::atomic<uint64_t> g_omniSampleTick{0};
static std::atomic<uint64_t> g_omniSampleUs{0};  // suma de µs muestreados
static std::atomic<uint64_t> g_omniSampleCount{0};
// Bucle cerrado neuro-acústico (v2.5.365), Ruta A: instancia propia del
// proceso app (paridad con IvannaFusionEngine::m_neuroLoop_ en Ruta B).
// Apagado por defecto (g_neuroLoopEnabledA=false). Sin frames reales
// (EEG/ABR/fNIRS por nativeNeuroLoopIngest) nunca se activa: compute()
// falla cerrado a identidad y NeuroBandEq es bypass bit-exacto.
// El reloj es CLOCK_MONOTONIC (monotonicNowUs()), el mismo dominio que
// deben usar los timestamps de los frames (Kotlin: uptimeMillis()*1000).
static ivanna::neuromorphic::ClosedLoopController g_neuroLoopRouteA;
static ivanna::neuromorphic::NeuroBandEq g_neuroBandEqRouteA;
static std::atomic<bool> g_neuroLoopEnabledA{false};
// Sensor acústico real por micrófono (v2.5.369), Ruta A: instancia propia
// del proceso app. El micrófono del teléfono (Android 13+) mide la salida
// real de los altavoces en la habitación/cabina y el bucle cerrado corrige
// el balance tonal en vivo (RoomCorrectionEq, bit-exacto si el bucle está
// apagado).
// La referencia (lo que sale al altavoz) la empuja nativeProcessBlock al
// anillo SPSC del sensor, sin mutex ni bloqueo en audio. Sin micrófono
// (sin permiso RECORD_AUDIO o sin captura) el sensor no publica frames
// válidos y la corrección se desvanece a 0 (fail-closed).
static ivanna::spatial::RealAcousticSensor g_acousticSensorA;
static ivanna::dsp::RoomCorrectionEq g_sensorRoomEqA;
static std::atomic<bool> g_sensorRoomCorrEnabledA{false};
static std::atomic<float> g_sensorRoomCorrAmountA{0.0f};
// Hiper-Operador de Estado Φ (v2.5.369), Ruta A: instancia propia del
// proceso app (paridad con IvannaFusionEngine::m_hyper en Ruta B).
// ACTIVO por defecto (amount=0.5, directiva explícita 2026-11-08;
// amount=0 → identidad bit-exacta).
// Se alimenta del SensorFrame vivo del micrófono (g_acousticSensorA):
// en modo MEDIDO corrige hacia el target élite (Harman 2018 por defecto,
// mismo target que el sensor); sin frames frescos cae a APROXIMADO
// (balance espectral hacia el mismo target, solo corte). Para cualquier
// Android 13+; el Moto G85 es solo el dispositivo de desarrollo.
static ivanna::spatial::HyperStateOperator g_hyperRouteA;
// g_hyperMeasuredA es local al JNI (guarda anti-doble-corrección). Los
// parámetros (amount/preset/xtc) viven en hyper_controls_bridge.cpp
// (patrón HOLOS: una sola fuente para Ruta A (JNI) y Ruta B (FusionCore);
// el stub del efecto está en hyper_globals_effect.cpp).
static std::atomic<bool> g_hyperMeasuredA{false};
// Externs de los atomics HOLOS/ORBE (definidos en holos/orbe_controls_
// bridge.cpp; las declaraciones completas están más abajo en este TU,
// pero los helpers del bucle cerrado viven aquí arriba).
extern std::atomic<float> g_holos_gamma;
// IVANNA: definidos en omni_controls_bridge.cpp (este target).
extern std::atomic<float> g_omni_amount;
extern std::atomic<float> g_omni_scene_pull;
extern std::atomic<float> g_omni_distance_m;
extern std::atomic<bool> g_omni_head_tracking;
extern std::atomic<float> g_omni_yaw_rad;
extern std::atomic<float> g_omni_audio_loss_l[6];
extern std::atomic<float> g_omni_audio_loss_r[6];
extern std::atomic<bool> g_omni_audiogram_dirty;
extern std::atomic<float> g_hyper_amount;
extern std::atomic<int> g_hyper_preset;
extern std::atomic<bool> g_hyper_xtc;
extern std::atomic<float> g_holos_width;
extern std::atomic<float> g_holos_diffuse;
extern std::atomic<float> g_holos_distance_m;
extern std::atomic<float> g_holos_proximity_db;
extern std::atomic<float> g_orbe_diffuse;
extern std::atomic<float> g_orbe_distance_m;
extern std::atomic<float> g_orbe_proximity_db;

// IVANNA (v2.5.375): tras HOLOS, antes de Φ. amount=0 = bypass bit-exacto
// (el operador también lo garantiza). Lee los atomics g_omni_* cada bloque.
// ORBE y HOLOS siguen activos. ÚNICO punto de llamada en Ruta A: los dos
// caminos de proceso (nativeProcess y nativeProcessBlock) pasan por aquí,
// así que la instrumentación (contadores + muestreo de CPU 1/64) cubre
// ambos sin duplicar código. Semántica idéntica al bloque inline anterior:
// amount<=0 o punteros/n inválidos ⇒ retorno sin tocar los buffers.
// Hilo de audio: sin log, sin allocations; el cronómetro es vDSO
// (monotonicNowUs) y solo corre 1 de cada 64 bloques.
static void omniProcessRouteA(float* l, float* r, int n) noexcept {
    const float omniAmount = g_omni_amount.load(std::memory_order_relaxed);
    if (omniAmount <= 0.0f || l == nullptr || r == nullptr || n <= 0) return;
    ivanna::omnifield::OmniParams op;
    op.amount = omniAmount;
    op.scenePull = g_omni_scene_pull.load(std::memory_order_relaxed);
    op.distanceM = g_omni_distance_m.load(std::memory_order_relaxed);
    op.headTracking = g_omni_head_tracking.load(std::memory_order_relaxed);
    g_omniRouteA.setParams(op);
    g_omniRouteA.setHeadPose(g_omni_yaw_rad.load(std::memory_order_relaxed));
    if (g_omni_audiogram_dirty.exchange(false, std::memory_order_relaxed)) {
        float lossL[6], lossR[6];
        for (int i = 0; i < 6; ++i) {
            lossL[i] = g_omni_audio_loss_l[i].load(std::memory_order_relaxed);
            lossR[i] = g_omni_audio_loss_r[i].load(std::memory_order_relaxed);
        }
        g_omniRouteA.setAudiogram(lossL, lossR);
    }
    const bool sample =
        (g_omniSampleTick.fetch_add(1, std::memory_order_relaxed) & 63u) == 0u;
    const uint64_t t0 =
        sample ? ivanna::neuromorphic::monotonicNowUs() : 0u;
    g_omniRouteA.process(l, r, n);
    if (sample) {
        const uint64_t dt = ivanna::neuromorphic::monotonicNowUs() - t0;
        g_omniSampleUs.fetch_add(dt, std::memory_order_relaxed);
        g_omniSampleCount.fetch_add(1, std::memory_order_relaxed);
    }
}

// 1× por bloque de audio, ANTES de HOLOS/ORBE. Aplica la ganancia por
// banda (NeuroBandEq) y devuelve los ajustes para modular hp/op.
// Hilo de audio; nunca se llama dos veces por bloque.
static ivanna::neuromorphic::NeuroAdjustments neuroLoopRouteAUpdate(
    float* left, float* right, int n, float sampleRate) noexcept {
    using namespace ivanna::neuromorphic;
    NeuroAdjustments identity{};
    if (!left || !right || n <= 0) return identity;
    if (!g_neuroLoopEnabledA.load(std::memory_order_relaxed)) return identity;
    const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
    NeuroAdjustments adj = g_neuroLoopRouteA.compute(monotonicNowUs());
    g_neuroBandEqRouteA.setGains(adj.gainDb, sr);
    g_neuroBandEqRouteA.process(left, right, n);
    return adj;
}

// Deltas locales sobre la base del usuario (atomics g_holos_*/g_orbe_*,
// que NUNCA se reescriben). Se aplican a las copias hp/op del bloque.
static ivanna::neuromorphic::ClosedLoopTargets neuroLoopTargetsHolos(
    const ivanna::neuromorphic::NeuroAdjustments& adj) noexcept {
    using namespace ivanna::neuromorphic;
    ClosedLoopTargets base{};
    base.holosGamma = g_holos_gamma.load(std::memory_order_relaxed);
    base.holosWidth = g_holos_width.load(std::memory_order_relaxed);
    base.holosDiffuse = g_holos_diffuse.load(std::memory_order_relaxed);
    base.orbeDistanceM = g_holos_distance_m.load(std::memory_order_relaxed);
    base.orbeProximityDb = g_holos_proximity_db.load(std::memory_order_relaxed);
    return applyAdjustments(base, adj);
}
static ivanna::neuromorphic::ClosedLoopTargets neuroLoopTargetsOrbe(
    const ivanna::neuromorphic::NeuroAdjustments& adj) noexcept {
    using namespace ivanna::neuromorphic;
    ClosedLoopTargets base{};
    base.holosDiffuse = g_orbe_diffuse.load(std::memory_order_relaxed);
    base.orbeDistanceM = g_orbe_distance_m.load(std::memory_order_relaxed);
    base.orbeProximityDb = g_orbe_proximity_db.load(std::memory_order_relaxed);
    return applyAdjustments(base, adj);
}
// Recorte térmico de la Ruta A (paridad con omega_effect.cpp, Ruta B). Lo escribe el
// ThermalGovernor Kotlin por JNI; el hilo de audio lo lee una vez por bloque.
//   g_holosTierA: HolosThermalTier 0 Normal / 1 Warm / 2 Limited / 3 Bypass.
//   g_spatialShedA: ORBE y NPE se apagan con fundido propio (mismo criterio que Ruta B:
//   tier >= LIMITED o sobrecarga de CPU).
static std::atomic<int>  g_holosTierA{0};
static std::atomic<bool> g_spatialShedA{false};
static GainStage      g_gain;
// Anti-ruido digital: DC-block + sanitizer NaN/Inf en el borde de salida.
static ivanna::DcBlocker g_dcBlockL, g_dcBlockR;
static SafetyLimiter  g_safety_limiter;
// OMEGA PERCEPTUAL GUARD
static OmegaPerceptualGuard g_perceptualGuard;

// IvannaLab — instancia única, alimentada bajo demanda desde nativeLabFeed().
// No vive en el hot-path de audio de ninguna ruta.
static ivanna::IvannaLab g_lab(48000, 4096);  // se ajusta a la tasa real en el auto-feed

// FASE 2 (NAEL): flag global — definición en audio_control_plane.cpp.
extern std::atomic<bool> g_nael_enabled;
extern std::atomic<bool> g_upmixing_enabled;
extern std::atomic<float> g_upmixing_immersivity;
extern std::atomic<bool>  g_wfs_enabled;
extern std::atomic<float> g_wfs_spread;
// ORBE: definidos en orbe_controls_bridge.cpp (este target). Mismo patrón que g_wfs_*.
extern std::atomic<float> g_orbe_amount;
extern std::atomic<float> g_orbe_distance_m;
extern std::atomic<float> g_orbe_spread;
extern std::atomic<float> g_orbe_proximity_db;
extern std::atomic<float> g_orbe_diffuse;
extern std::atomic<bool>  g_orbe_head_tracking;
extern std::atomic<float> g_orbe_head_radius_m;  // Ψ: radio personalizado (KEMAR 0.0875 por defecto)
// HOLOS: definidos en holos_controls_bridge.cpp (este target).
extern std::atomic<float> g_holos_amount;
extern std::atomic<float> g_holos_distance_m;
extern std::atomic<float> g_holos_spread;
extern std::atomic<float> g_holos_proximity_db;
extern std::atomic<float> g_holos_diffuse;
extern std::atomic<float> g_holos_harmonics;
extern std::atomic<float> g_holos_warmth;
extern std::atomic<float> g_holos_dynamics;
extern std::atomic<float> g_holos_width;
extern std::atomic<float> g_holos_gamma;
// IVANNA: definidos en omni_controls_bridge.cpp (este target).
extern std::atomic<float> g_omni_amount;
extern std::atomic<float> g_omni_scene_pull;
extern std::atomic<float> g_omni_distance_m;
extern std::atomic<bool> g_omni_head_tracking;
extern std::atomic<float> g_omni_yaw_rad;
extern std::atomic<float> g_omni_audio_loss_l[6];
extern std::atomic<float> g_omni_audio_loss_r[6];
extern std::atomic<bool> g_omni_audiogram_dirty;
extern std::atomic<float> g_hyper_amount;
extern std::atomic<int> g_hyper_preset;
extern std::atomic<bool> g_hyper_xtc;
extern std::atomic<float> g_holos_room_rt60;
extern std::atomic<bool>  g_holos_head_tracking;
extern std::atomic<int>   g_holos_band_count;   // 12 o 24 bandas ERB (24 = default)
extern std::atomic<float> g_holos_eq_low;
extern std::atomic<float> g_holos_eq_mid;
extern std::atomic<float> g_holos_eq_high;
extern std::atomic<float> g_holos_eq_presence;
extern std::atomic<float> g_holos_eq_tilt_db;
extern std::atomic<float> g_holos_eq_freq;
extern std::atomic<bool>  g_aux_field_enabled;
extern std::atomic<float> g_aux_field_xover_hz;
extern std::atomic<float> g_aux_field_coherence;
extern std::atomic<bool>  g_temporal_field_enabled;
extern std::atomic<float> g_temporal_arc_tension;
extern std::atomic<float> g_temporal_arc_impact;
extern std::atomic<float> g_temporal_field_amount;
extern std::atomic<bool>  g_temporal_field_auto;
extern std::atomic<float> g_room_corr_db[10];

// FASE 3 (IvannaLab auto-feed): gate ON/OFF + contador de feeds. El feed
// real ocurre dentro de nativeProcess (post-procesado) con throttle 1/100
// bloques — contador thread_local en la función, sin golpear atomic por bloque.
static std::atomic<bool> g_lab_auto_enabled{true};
static std::atomic<int>  g_lab_auto_frame_count{0};

// ═══════════════════════════════════════════════════════════════════════════
// SUPREMACÍA ACÚSTICA: Volterra H2, FastRPC Hexagon cDSP & Active Transducer Inversion
// Soporta tanto Root (AudioFlinger / Daemon) como Non-Root (In-Process Pipeline)
// ═══════════════════════════════════════════════════════════════════════════
// v2.5.369: Volterra Genezi ACTIVO por defecto (directiva: todo lo nuevo
// suena desde la primera canción). El kernel se carga al arrancar
// (PersistedStateRestorer) y el motor queda habilitado salvo que el
// usuario lo apague (persistido en ParameterStore). Sin kernel cargado el
// motor es passthrough bit-exacto (processInterleaved copia la entrada),
// así que encendido-sin-kernel no silencia ni distorsiona.
static std::atomic<bool> g_volterra_enabled{true};
static std::atomic<bool> g_fastrpc_enabled{false};
static std::atomic<bool> g_ati_enabled{true};
static ivanna::dsp::VolterraH2Symmetric g_volterra_engine{64, 2};

// ── Eje Supremo Neuroacústico: Inversión Biomecánica Coclear Activa ──────────
// Definidos aquí con enlace externo; ivanna_spatial_jni.cpp los declara extern
// para que sus helpers (Ruta B) los lean sin pasar por JNI adicional.
// g_cochlearEngine  : motor DSP RT-Safe (≈704 bytes, sin heap)
// g_cochlearEnabled : flag atómico leído en hot-path (relaxed)
// g_cochlearIntensity: nivel wet [0..1] leído en hot-path (relaxed)
#include "../neuromorphic/CochlearActiveInverseModel.hpp"
#include "../include/omega_wave_stages.hpp"
#include "../supreme/SpatialWidthReadback.hpp"

// ── Ruta A: geometría de sala → cancelador de diafonía 4D ───────────────────────────────────────────────
// El cancelador de la cadena 4D (compartida con Ruta B) corría con la geometría de escritorio (0.90 m / 0.85 m)
// porque, en Ruta A, nadie le pasaba la de la sala. Se deriva de FL/FR del mismo layout que usa el WFS de
// Ruta A (ya trasladado al marco del oyente nativo fijo) con la misma función que Ruta B. fourd::setSpeakerGeometry
// guarda el valor y lo aplica cuando la cadena exista; si nada cambió no recalcula.
static void routeAApplyXtcGeometry(const float* x, const float* y, const float* z) noexcept {
    const auto& room = ivanna::spatial::RoomGeometryConfig::defaultLayout();
    const auto pl = ivanna::spatial::RoomGeometryConfig::crosstalkPlacement(
        x, y, z, room.listenerX, room.listenerY, room.listenerZ);
    if (!pl.valid) return;
    ivanna::spatial::CrosstalkCanceller::Geometry g;
    g.spanMeters = pl.spanM; g.distanceMeters = pl.distanceM; g.listenerOffsetM = pl.listenerOffsetM;
    ivanna::fourd::setSpeakerGeometry(g);
}
// ── Ruta A: EQ evolutivo de 512 bandas (paridad con Ruta B: IvannaFusionEngine::m_evoEq) ───────────────
// Antes solo existia en el efecto del sistema (Ruta B). La calibracion (LM-CMA-ES acotada, ~0.6 s en host)
// NUNCA corre en el hilo de audio ni bloquea nativeInit: un hilo la ejecuta sobre una instancia propia y la
// publica con un atomico (release). Hasta entonces el efecto es identidad bit-exacta. La instancia publicada
// no se libera (vive lo que el proceso; ~15 KB) para que el hilo de audio nunca vea un puntero colgante.
// El filtro es el mismo de Ruta B (FIR de 256 taps hacia la curva objetivo del modulo); entra y sale con un
// crossfade de 1 bloque para que activarlo/desactivarlo no haga click.
static std::atomic<Ivanna::EvolutionaryEQ*> g_evoEqA{nullptr};
static std::atomic<bool> g_evoEqAEnabled{true};   // por defecto ON: Ruta B lo aplica siempre una vez calibrado
static std::atomic<bool> g_evoEqACalibrating{false};
static float g_evoEqAWet = 0.0f;                  // solo lo toca el hilo de audio (bajo g_dspProcessMutex)

static void* routeAEvoEqCalibrationWorker(void*) noexcept {
    Ivanna::EvolutionaryEQ* eq = new (std::nothrow) Ivanna::EvolutionaryEQ();
    if (eq) {
        eq->calibrate(48000.0f);
        if (eq->isReady()) {
            g_evoEqA.store(eq, std::memory_order_release);
            eq = nullptr;  // propiedad transferida al proceso; el hot-path nunca libera el objeto
        }
    }
    delete eq;  // asignacion fallida/no verificada: Ruta A permanece en identidad
    g_evoEqACalibrating.store(false, std::memory_order_release);
    return nullptr;
}

static void routeAStartEvoEqCalibration() noexcept {
    if (g_evoEqA.load(std::memory_order_acquire) != nullptr) return;  // calibracion valida: no recrear ni filtrar el puntero
    bool expected = false;
    if (!g_evoEqACalibrating.compare_exchange_strong(expected, true)) return;   // una sola vez mientras calibra

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        g_evoEqACalibrating.store(false, std::memory_order_release);
        return;
    }
    const int detachRc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t worker{};
    const int createRc = (detachRc == 0)
        ? pthread_create(&worker, &attr, routeAEvoEqCalibrationWorker, nullptr)
        : detachRc;
    pthread_attr_destroy(&attr);
    if (createRc != 0) {
        // Fallo de creacion: permitir que el siguiente nativeInit vuelva a intentarlo.
        g_evoEqACalibrating.store(false, std::memory_order_release);
    }
}

static void routeAApplyEvoEq(float* L, float* R, int n) noexcept {
    Ivanna::EvolutionaryEQ* eq = g_evoEqA.load(std::memory_order_acquire);
    const float target = (eq && g_evoEqAEnabled.load(std::memory_order_relaxed)) ? 1.0f : 0.0f;
    if (!eq || (g_evoEqAWet == 0.0f && target == 0.0f)) { g_evoEqAWet = target; return; }
    if (n <= 0 || n > 2048) return;
    float dryL[2048], dryR[2048];
    std::memcpy(dryL, L, n * sizeof(float));
    std::memcpy(dryR, R, n * sizeof(float));
    // Al reactivar (wet==0 -> target==1) el historial FIR es obsoleto (no
    // avanzó mientras estaba apagado): se vacía para no convolver con audio
    // viejo. El crossfade de 1 bloque enmascara el arranque limpio.
    if (g_evoEqAWet == 0.0f && target == 1.0f) eq->resetHistory();
    eq->processPlanar(L, R, static_cast<size_t>(n));   // el historial FIR avanza siempre: sin salto al reactivar
    const float w0 = g_evoEqAWet;
    if (w0 == 1.0f && target == 1.0f) return;          // estado estable: salida 100 % procesada
    const float inv = 1.0f / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const float w = w0 + (target - w0) * (static_cast<float>(i + 1) * inv);
        L[i] = dryL[i] + w * (L[i] - dryL[i]);
        R[i] = dryR[i] + w * (R[i] - dryR[i]);
    }
    g_evoEqAWet = target;
}

// ── Ruta A: NPE neuromorfico en la cadena nativa (paridad con Ruta B: IvannaFusionEngine::m_npe) ────────
// Con el motor nativo activo, PlaybackCaptureService NO ejecuta IvannaNpeEngine (Kotlin) para no apilar etapas:
// el interruptor y los parametros del NPE no tenian efecto en la ruta que realmente intercepta el audio. Se usa
// NpeStereoCore (misma cadena por muestra que el modulo, sin asignaciones) justo antes del punch/limitador final.
// Apagado y con fade 0 es bit-exacto. Parametros por atomicos (control -> audio); mismos rangos que Ruta B.
static ivanna::neuromorphic::NpeStereoCore g_npeA;
static std::atomic<bool>  g_npeAOn{false};          // como Ruta B: apagado hasta que la app lo publique
static std::atomic<float> g_npeAHarm{0.2f}, g_npeALat{0.2f}, g_npeAOhc{0.3f}, g_npeAMasterDb{0.0f};
static std::atomic<float> g_npeAPreparedSr{0.0f};   // solo se (re)prepara en nativeInit, bajo g_dspProcessMutex

static void routeAApplyNpe(float* L, float* R, int n) noexcept {
    g_npeA.setParams(g_npeAOn.load(std::memory_order_relaxed) &&
                         !g_spatialShedA.load(std::memory_order_relaxed),
                     g_npeAHarm.load(std::memory_order_relaxed),
                     g_npeALat.load(std::memory_order_relaxed),
                     g_npeAOhc.load(std::memory_order_relaxed),
                     g_npeAMasterDb.load(std::memory_order_relaxed));
    g_npeA.process(L, R, n);
}

static void routeAApplyDefaultXtcGeometry() noexcept {
    const auto& room = ivanna::spatial::RoomGeometryConfig::defaultLayout();
    routeAApplyXtcGeometry(room.speakerX.data(), room.speakerY.data(), room.speakerZ.data());
}
std::atomic<bool>  g_cochlearEnabled{false};
std::atomic<float> g_cochlearIntensity{0.35f};
ivanna::neuromorphic::CochlearActiveInverseEngine g_cochlearEngine;
static ivanna::spatial::IvannaAudioPipeline g_liveAudioPipeline;
static ivanna::unified::DeclarativeUnifiedPipeline g_unifiedPipeline;
extern "C" void ivanna_pilstm_bridge_tick_block(const float* inL, const float* inR, int frames, float sampleRate) noexcept;
namespace {
struct LivePipelineAutoRegistrar {
    LivePipelineAutoRegistrar() noexcept {
        ivanna::spatial::IvannaAudioPipeline::setActiveInstance(&g_liveAudioPipeline);
    }
} g_livePipelineRegistrar;
} // namespace

static PDEngine       g_pd;    // NHO + BiquadEnvelopeBank + CueBasedSpatial
static ivanna::spatial::RouteAWfsEngine g_routeAWfsEngine;

// AUX has priority over the binaural WFS renderer in Route A, matching the
// Route-B spatial policy. Keep the user's WFS preference intact so disabling
// AUX restores WFS automatically; RouteAWfsEngine applies the 15 ms crossfade.
static inline bool routeAWfsAllowed() noexcept {
    return ivanna::spatial::isRouteAWfsAllowed(
        g_wfs_enabled.load(std::memory_order_acquire),
        g_aux_field_enabled.load(std::memory_order_acquire));
}

static inline void processRouteAWfs(float* left, float* right, int frames) noexcept {
    g_routeAWfsEngine.setEnabled(routeAWfsAllowed());
    g_routeAWfsEngine.process(left, right, frames);
}
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
static std::atomic<float>         g_rirPendingSynthRt60{-1.0f};
static std::atomic<float>         g_localRoomRt60{0.34f};
static std::atomic<float>         g_localRoomWet{0.22f};
static std::atomic<int32_t>       g_localRoomIdx{51};
static float                      g_localSafQ[7] = {
    4.00711025e-03f, 0.00000000e+00f, 5.49971378e-03f, 4.70094676e-03f,
    -3.88360593e-03f, 4.82163913e-03f, 9.68858446e-03f
};
static std::mutex                 g_rirWorkerMtx;
static std::condition_variable    g_rirWorkerCv;
static std::atomic<bool>          g_rirWorkerRunning{false};
static std::thread                g_rirWorkerThread;
static void rirWorkerLoop() {
    std::unique_lock<std::mutex> lk(g_rirWorkerMtx);
    while (true) {
        g_rirWorkerCv.wait(lk, [] {
            return !g_rirWorkerRunning.load(std::memory_order_acquire)
                || g_rirPendingIdx.load(std::memory_order_acquire) >= 0
                || g_rirPendingSynthRt60.load(std::memory_order_acquire) > 0.0f;
        });
        if (!g_rirWorkerRunning.load(std::memory_order_acquire)) return;
        const float synthRt60 = g_rirPendingSynthRt60.exchange(-1.0f, std::memory_order_acq_rel);
        const int32_t idx = g_rirPendingIdx.exchange(-1, std::memory_order_acq_rel);
        Ivanna::RirDataset*   ds   = g_rirDataset.load(std::memory_order_acquire);
        Ivanna::RirConvolver* conv = g_rirConvolver.load(std::memory_order_acquire);
        if (!conv) continue;
        if (synthRt60 > 0.0f && idx < 0) {
            lk.unlock();
            if (g_localRoomRt60.load(std::memory_order_acquire) >= 0.01f &&
                g_localRoomWet.load(std::memory_order_acquire) > 0.001f) {
                conv->applySofaCoupling(g_localSafQ);
                conv->synthesizeMasterStudioBrir(synthRt60, (int)g_params.sampleRate);
            }
            lk.lock();
            continue;
        }
        if (idx < 0 || !ds) continue;
        lk.unlock();  // no retener el mutex durante disco + FFT
        std::vector<float> irL, irR;
        int sr = 0;
        if (ds->loadImpulseResponse((size_t)idx, irL, irR, sr) && !irL.empty()) {
            const int sessionSr = (int)g_params.sampleRate;
            Ivanna::RirDataset::resampleLinear(irL, sr, sessionSr);
            Ivanna::RirDataset::resampleLinear(irR, sr, sessionSr);
            int irLen = (int)irL.size();
            if (irLen > Ivanna::RirConvolver::MAX_IR_TOTAL) irLen = Ivanna::RirConvolver::MAX_IR_TOTAL;
            if (g_localRoomRt60.load(std::memory_order_acquire) >= 0.01f &&
                g_localRoomWet.load(std::memory_order_acquire) > 0.001f) {
                conv->setSampleRate(sessionSr);
                conv->applySofaCoupling(g_localSafQ);
                conv->load(irL.data(), irR.data(), irLen);
            } else {
                conv->setWetDry(0.0f);
                conv->unload();
            }
        }
        lk.lock();
    }
}
// FEATURE (Voice Protection): 0..1, cuánta voz detecta YamnetClassifier en
// el bloque actual. Protege la inteligibilidad de la voz mezclando de
// vuelta hacia la señal seca (pre-DSP) cuando hay voz dominante, en vez de
// dejar que Exciter/Compresor la sobre-procesen.
static std::atomic<float> g_voice_protect_score{0.f};
static std::atomic<bool>  g_voice_protect_score_valid{false};

// ── FIX nho_wet: 3 escritores competían sobre g_pd.set_nho_wet() ─────────────
// nativeSetParams pisaba el control HRTF WET y el η del LSTM en cada movimiento
// de slider. Se separan en 3 atómicos; applyNhoWet() los combina y escribe una
// sola vez. Valores iniciales: exciter neutro, spatial ON, eta neutro.
static std::atomic<float> g_nho_wet_exciter{0.28f};
static std::atomic<float> g_nho_wet_spatial{1.0f};
static std::atomic<float> g_nho_wet_eta    {0.72f};

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
    float loudnessTrimSmooth   = 1.0f;
    uint64_t lastAdaptiveSeq   = 0;
    // FIX (tronido "metralleta" al subir volumen): estado persistente del
    // pre-EQ peak guard para poder rampear en vez de saltar de bloque a
    // bloque. Separado en dos campos por el mismo motivo que blkTgSmooth
    // ya está separado de targetGainSmooth arriba: nativeProcess y
    // nativeProcessBlock pueden correr en hilos distintos.
    float peakGuardScale       = 1.0f;
    // Diagnóstico: cuántas muestras tuvieron que sanearse por NaN/Inf. Debe
    // quedarse en 0 en operación normal — si sube, hay una etapa divergiendo
    // de verdad río arriba (ver el saneo final antes del re-intercalado).
    uint64_t nanRecoveries     = 0;
    // nativeProcessBlock path (puede correr en thread distinto a nativeProcess)
    float blkTgSmooth          = 1.0f;
    float blkCaSmooth          = 0.0f;
    float blkErSmooth          = 0.0f;
    float blkPeakGuardScale    = 1.0f;
    uint64_t blkNanRecoveries  = 0;
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
static std::atomic<bool>  g_loudness_trim_enabled{true};
// ═══ FASE 4B: AdaptiveDecisionEngine — cierre del lazo adaptativo ════════════
// Única instancia del motor. Sus buses (rawMetrics, adaptiveState) son
// SPSC seqlock — el audio thread publica RawAudioMetrics + consume
// AdaptiveState, el hilo de control interno hace el trabajo lento.
// g_adaptiveEngineStarted evita doble start() si nativeInit se llama más
// de una vez (p. ej. cambio de sample rate por reproductor de archivo).
static ivanna::experimental::AdaptiveDecisionEngine g_adaptiveEngine;
static std::atomic<bool> g_adaptiveEngineStarted{false};
// La preferencia del usuario es la autoridad. El bootstrap de DSP puede
// repetirse por cambio de SR, pero nunca debe reactivar el motor si está en
// modo manual o deshabilitado.
static std::atomic<bool> g_adaptiveEngineUiEnabled{true};
static std::mutex g_adaptiveEngineLifecycleMutex;

static bool ensureAdaptiveEngineStarted() noexcept {
    std::lock_guard<std::mutex> lock(g_adaptiveEngineLifecycleMutex);
    // Both explicit create() and watchdog recovery are gated by a fully
    // initialized native DSP graph. A preference can remain ON while the
    // graph is unavailable; the next successful nativeInit retries here.
    if (!g_initialized.load(std::memory_order_acquire)) {
        if (g_adaptiveEngine.running()) g_adaptiveEngine.stop();
        g_adaptiveEngineStarted.store(false, std::memory_order_release);
        return false;
    }
    if (!g_adaptiveEngineUiEnabled.load(std::memory_order_acquire)) {
        if (g_adaptiveEngine.running()) g_adaptiveEngine.stop();
        g_adaptiveEngineStarted.store(false, std::memory_order_release);
        return false;
    }
    const bool created = g_adaptiveEngine.start();
    const bool live = created && g_adaptiveEngine.running();
    g_adaptiveEngineStarted.store(live, std::memory_order_release);
    return live;
}

static bool setAdaptiveEngineEnabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(g_adaptiveEngineLifecycleMutex);
    g_adaptiveEngineUiEnabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        g_adaptiveEngine.stop();
        g_adaptiveEngineStarted.store(false, std::memory_order_release);
        return false;
    }
    // A user ON preference may arrive before DSP initialization. Preserve
    // the preference, but do not create a control thread until the native
    // graph is initialized; nativeInit will call ensureAdaptiveEngineStarted.
    if (!g_initialized.load(std::memory_order_acquire)) {
        g_adaptiveEngineStarted.store(false, std::memory_order_release);
        return false;
    }
    const bool created = g_adaptiveEngine.start();
    const bool live = created && g_adaptiveEngine.running();
    g_adaptiveEngineStarted.store(live, std::memory_order_release);
    return live;
}
// Snapshot del último AdaptiveState publicado — lo leen los JNI getters de
// telemetría (fuera del audio thread) para exponer el ciclo a Kotlin/UI.
// Se actualiza dentro del audio thread justo después de consumeIfNewer().
static std::atomic<float> g_lastAdaptiveTargetGain{1.0f};
static std::atomic<float> g_lastAdaptiveCompAmount{0.0f};
static std::atomic<float> g_lastAdaptiveExcReduction{0.0f};
static std::atomic<float> g_lastAdaptiveSpatialWidth{1.0f};
static std::atomic<float> g_lastAdaptiveSafetyMargin{1.0f};
static std::atomic<float> g_lastAdaptiveVoiceProtect{0.0f};
// v2.5.15 — salidas del arbitraje unificado (Fase 3) del ADE que se calculaban y NADIE aplicaba en
// Ruta A (rir_wet_scale, ms_widener_scale, eq_tilt_db). Se aplican mezcladas por la fuerza adaptativa.
static std::atomic<float> g_lastAdaptiveRirWetScale{1.0f};
static std::atomic<float> g_lastAdaptiveMsWidenerScale{1.0f};
static std::atomic<float> g_lastAdaptiveEqTiltDb{0.0f};
// Interruptor del usuario para el Acoustic Unity Engine (default ON = comportamiento de v2.5.14).
static std::atomic<bool>  g_unity_enabled{true};
// v2.5.41: impacto predictivo (PunchEngine). 0 = apagado = bypass exacto y latencia 0. Es la misma instancia para nativeProcess y
// nativeProcessBlock (nunca corren a la vez: g_dspProcessMutex).
static ivanna::PunchEngine g_punch;
static std::atomic<float> g_lastRawRms{0.0f};
// Adaptive Control Center: modula el mismo motor adaptativo productivo (Calibración Magistral STUDIO 82% por defecto).
static std::atomic<int>   g_adaptiveUiMode{2};      // 0=OFF,1=NATURAL,2=STUDIO,3=EXTREME
static std::atomic<float> g_adaptiveUiIntensity{82.f};
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

// ── Telemetría IVANNA (OmniField, Ruta A) — log con límite de frecuencia ──
// Hilo de control (adaptiveSnapshotLoop, 20 ms), NUNCA el hilo de audio:
// max 1 línea / 5 s y solo si hubo bloques renderizados nuevos o cambió
// fault. Sin allocations: solo lecturas atómicas + LOGI. Es la evidencia en
// logcat (tag IVANNA-JNI) de que OmniField procesa de verdad en el teléfono.
// El primer log con actividad sale inmediato (no espera 5 s). La media de
// µs/bloque viene del muestreo 1/64 de omniProcessRouteA; la EMA del DSP
// total y los bloques en shed ya los publica logDspShedStateThrottled().
static void logOmniTelemetryThrottled() noexcept {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point lastLog{};
    static uint64_t lastBlocks = 0, lastFrames = 0;
    static bool lastFault = false;
    static bool firstLog = true;
    const uint64_t blocks = g_omniRouteA.blocksRendered();
    const uint64_t frames = g_omniRouteA.framesRendered();
    const bool fault = g_omniRouteA.faulted();
    if (blocks == lastBlocks && frames == lastFrames && fault == lastFault) return;
    const auto now = Clock::now();
    if (!firstLog && now - lastLog < std::chrono::seconds(5)) return;
    firstLog = false;
    lastLog = now;
    const uint64_t dBlocks = blocks - lastBlocks;
    const uint64_t dFrames = frames - lastFrames;
    lastBlocks = blocks;
    lastFrames = frames;
    lastFault = fault;
    const uint64_t sN = g_omniSampleCount.load(std::memory_order_relaxed);
    const uint64_t sUs = g_omniSampleUs.load(std::memory_order_relaxed);
    const double meanUs = (sN > 0) ? (double)sUs / (double)sN : 0.0;
    LOGI("IVANNA OmniField Ruta A: +%llu bloques/+%llu frames desde ultimo log (total %llu/%llu), wet %.2f, r %.3f, fault %d, omni %.0f us/bloque (muestreo 1/64, n=%llu)",
         static_cast<unsigned long long>(dBlocks),
         static_cast<unsigned long long>(dFrames),
         static_cast<unsigned long long>(blocks),
         static_cast<unsigned long long>(frames),
         (double)g_omniRouteA.appliedAmount(),
         (double)g_omniRouteA.convergenceR(),
         fault ? 1 : 0,
         meanUs,
         static_cast<unsigned long long>(sN));
}

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
            g_lastAdaptiveRirWetScale   .store(st.rir_wet_scale,         std::memory_order_release);
            g_lastAdaptiveMsWidenerScale.store(st.ms_widener_scale,      std::memory_order_release);
            g_lastAdaptiveEqTiltDb      .store(st.eq_tilt_db,            std::memory_order_release);
        }
        // Telemetría IVANNA (hilo de control): log con límite de frecuencia.
        logOmniTelemetryThrottled();
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
static std::atomic<bool>     g_audioRouteBridgeStarted{false};
static std::atomic<uint32_t> g_routeB_declippedPeaks{0u};
static std::atomic<float>    g_routeB_tapeMag{0.0f};
static std::atomic<uint32_t> g_routeB_snnSpikes{2u};
static std::atomic<float>    g_routeB_subsampleDelay{0.24f};

extern "C" uint32_t ivanna_get_routeB_declipped_peaks() noexcept {
    return g_routeB_declippedPeaks.load(std::memory_order_relaxed);
}
extern "C" float ivanna_get_routeB_tape_mag() noexcept {
    return g_routeB_tapeMag.load(std::memory_order_relaxed);
}
extern "C" uint32_t ivanna_get_routeB_snn_spikes() noexcept {
    return g_routeB_snnSpikes.load(std::memory_order_relaxed);
}
extern "C" float ivanna_get_routeB_subsample_delay() noexcept {
    return g_routeB_subsampleDelay.load(std::memory_order_relaxed);
}
extern "C" bool ivanna_is_routeB_active() noexcept {
    return g_activeRoute.load(std::memory_order_relaxed) == 2;
}
static void audioRouteBridgeLoop() {
    // Última lectura vista, para no republicar/loguear si omega_effect no
    // está produciendo audio nuevo ahora mismo (evita contaminar el bus
    // con ceros repetidos cuando Spotify/YouTube no están sonando).
    float lastRms = -1.0f, lastPeak = -1.0f;
    uint64_t frameCounter = 0;
    auto lastLogTime = std::chrono::steady_clock::now();
    // Reader dedicado: no compartir el singleton effectControlBus() usado
    // por otros setters/lectores JNI del plano de control de la app.
    static ivanna::OmegaControlBus s_routeBTelemetryReader;
    while (g_bridgeRunning.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        // Este puente app-side solo observa el bus local del efecto, que
        // publica raw_rms/raw_peak cuando no hay un writer daemon. Los campos
        // ai_raw_* de OmegaSharedState no tienen productor en este árbol:
        // leerlos cuando shared!=nullptr convertía sus ceros iniciales en
        // "silencio" aunque el AudioEffect estuviera procesando PCM.
        float rms = 0.0f, peak = 0.0f;
        static bool     s_busOpen = false;
        static uint64_t s_retryMs = 0;
        if (!s_busOpen) {
            const auto nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (nowMs >= s_retryMs) {
                s_busOpen = s_routeBTelemetryReader.openReader(
                                ivanna::OMEGA_EFFECT_LOCAL_BUS_PATH);
                s_retryMs = nowMs + 1000ULL;
            }
        }
        if (!s_busOpen) continue;
        static uint64_t s_lastGen = 0;
        ivanna::OmegaDspSnapshot snap{};
        if (s_routeBTelemetryReader.readLatest(snap, s_lastGen)) {
            rms  = snap.raw_rms;
            peak = snap.raw_peak;
            g_routeB_declippedPeaks.store(snap.supreme_declipped_peaks, std::memory_order_relaxed);
            g_routeB_tapeMag.store(snap.supreme_tape_mag, std::memory_order_relaxed);
            g_routeB_snnSpikes.store(snap.supreme_snn_spikes, std::memory_order_relaxed);
            if (snap.supreme_subsample_delay > 0.0f) {
                g_routeB_subsampleDelay.store(snap.supreme_subsample_delay, std::memory_order_relaxed);
            }
        } else if (lastRms < 0.0f) {
            continue;  // el bus local aún no publicó un bloque PCM real
        } else {
            rms = lastRms;
            peak = lastPeak;
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
        // Este observador no tiene GR sincronizada con el bloque PCM del
        // bus local. La ruta real publica SafetyLimiter GR dentro del ADE
        // per-instance de omega_effect; aquí se deja neutral.
        const float grDb = 0.0f;
        ivanna::experimental::RawAudioMetrics rawM{};
        rawM.rms               = rms;
        rawM.peak              = peak;
        // La memoria compartida conserva estos campos por compatibilidad ABI,
        // pero este build no tiene un productor verificable para ai_band_*.
        // Tampoco hay timestamp/route-generation unido al último score YAMNet:
        // no se puede asegurar que corresponda a este bloque PCM. La ruta B real
        // usa el RtBandMeter y el clasificador per-instance de omega_effect.cpp.
        // Este puente global conserva solo RMS/peak y una GR del AGC cuando está
        // habilitado; bandas y voz permanecen explícitamente no disponibles.
        rawM.band_energy_valid = false;
        rawM.voice_score_valid = false;
        rawM.gain_reduction_db = grDb;
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
        // La actuación SYSTEM_WIDE pertenece al ADE per-instance del
        // omega_effect: consume el PCM local de AudioFlinger y sus actuadores
        // viven en ese mismo contexto. Este observador global no escribe
        // ai_runtime_* sobre una SHM que no es suya ni reporta falsa aplicación.
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
/// Returns zero unless the user's adaptive preference is ON and the ADE worker is live.
static inline float adaptive_ui_strength() noexcept {
    // Fail closed when the ADE is disabled, not initialized, or its control
    // thread failed to start. Stopping the worker alone must not leave its
    // last non-neutral snapshot acting on Route A indefinitely (manual mode).
    if (!g_adaptiveEngineUiEnabled.load(std::memory_order_acquire) ||
        !g_adaptiveEngineStarted.load(std::memory_order_acquire)) {
        return 0.0f;
    }
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
    if (env->GetArrayLength(src) < n) return false;   // evita sobre-lectura del heap
    jfloat* p = env->GetFloatArrayElements(src, nullptr);
    if (!p) return false;
    memcpy(dst, p, n * sizeof(float));
    env->ReleaseFloatArrayElements(src, p, JNI_ABORT);
    return true;
}
// Banderas con las que se compiló ESTE binario (las inyecta CMakeLists.txt desde BASE_FLAGS).
// En builds host (CTest) que no pasan por ese CMake queda "unknown".
#ifndef IVANNA_NATIVE_FLAGS
#define IVANNA_NATIVE_FLAGS "unknown"
#endif
extern "C" {
// Forward declaration: hyperProcessRouteA se DEFINE más abajo en este mismo
// bloque extern "C" (~línea 2495) pero se LLAMA en el procesado de Ruta A
// (~línea 1749, antes de la definición). Sin esta declaración el NDK Clang
// falla con "use of undeclared identifier 'hyperProcessRouteA'"
// (CI run 42563522464). DEBE estar dentro de este bloque extern "C": la
// definición vive aquí dentro (enlace C); una declaración fuera del bloque
// (enlace C++) conflictúa con ella. El CTest host no detecta el error
// porque no compila este TU completo; solo el build Gradle/CMake de Android.
static void hyperProcessRouteA(float* oL, float* oR, int n, float sr) noexcept;
// ═══════════════════════════════════════════════════════════════════════════════
// DSPBridge (com.ivanna.omega.dsp.DSPBridge) — called at app startup
// ═══════════════════════════════════════════════════════════════════════════════
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("IVANNA OMEGA SUPREME v1.1-OPE | GORE TNS © 2026");
}
// Perfilador: flags de compilación reales del binario cargado (no texto fijo en la UI).
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetBuildFlags(JNIEnv* env, jobject) {
    return env->NewStringUTF(IVANNA_NATIVE_FLAGS);
}
// ── FIX: mutex DSP — declarado antes de nativeInit, nativeSetParams y nativeProcess ──
static std::mutex g_dspProcessMutex;
static std::mutex g_uiMutex;
static ivanna::DSPParams g_params_ui;
static std::atomic<bool> g_params_dirty{false};
static std::atomic<bool> g_eq_dirty{false};
static std::atomic<bool> g_comp_dirty{false};
static std::atomic<float> g_compPendingAttackMs{-1.0f};
static std::atomic<float> g_compPendingReleaseMs{-1.0f};

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeInit(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 384000) { LOGE("Bad SR: %d", sr); return; }
    if (g_initialized.load(std::memory_order_acquire)
        && (g_params.sampleRate == static_cast<uint32_t>(sr))) {
        // DSP ya listo (p. ej. inicializado por nativeInitDSP): una preferencia ON recibida
        // antes no debe quedarse sin motor. ensure() es idempotente y respeta OFF/manual.
        (void)ensureAdaptiveEngineStarted();
        return;
    }
    std::lock_guard<std::mutex> lock(g_dspProcessMutex);
    const bool alreadyInitSameSr = g_initialized.load(std::memory_order_acquire)
                                && (g_params.sampleRate == static_cast<uint32_t>(sr));
    if (alreadyInitSameSr) {
        (void)ensureAdaptiveEngineStarted();
        return;
    }
    // NATIVOS DIRECTOS: 48000/96000/192000/384000 Hz pasan sin remuestreo
    // ni re-cuantización. Antes el gate rechazaba 384k ("Bad SR") y todo
    // lo que no fuera <=192k moría aquí — el DSP completo (EQ, compresor,
    // exciter, widener, PDEngine, loudness meter) YA acepta cualquier SR
    // vía g_params.sampleRate/g_pd.init()/g_loudnessMeter.init(), solo el
    // gate lo bloqueaba. Sin cambio de arquitectura: misma cadena, gate
    // ampliado al rango que el hardware USB/Hi-Fi actual expone.
    g_params.sampleRate = (uint32_t)sr;
    if (!g_routeAWfsEngine.prepare(static_cast<float>(sr), 2048)) {
        LOGE("Ruta A WFS: inicialización fallida; salida permanece en bypass");
    }
    routeAApplyDefaultXtcGeometry();
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    // FIX (headroom EQ en el arranque): nativeInit era el único sitio de los
    // cuatro que llamaba a g_gain.setParams() SIN restar la compensación de
    // stack de bandas (nativeSetEQParams ~linea 670, el bloque con mutex
    // ~linea 714 y nativeInitDirect ~linea 1246 ya lo hacen). Con boosts
    // grandes (Agudos +8.4 dB + shelf + presencia) el EQ puede apilar >6 dB y
    // el SafetyLimiter arrancaba trabajando en modo pared desde la primera
    // muestra — el "truena al aplicar ISO" en cold-start venía de aquí.
    // Misma forma que los otros sitios: resta temporal, setParams, restaurar.
    { const float mDb = g_params.master;
      const float c   = g_eq.getOutputCompensationDb();
      g_params.master = std::clamp(mDb - c, -60.0f, 6.0f);
      g_gain.setParams(g_params);
      g_params.master = mDb; }
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
    g_safety_limiter.setParams(0.891251f, 0.98855f);  // umbral -1 dBFS (antes -4: limitaba siempre)
    g_safety_limiter.setSampleRate((float)sr);
    g_crossfeed.setSampleRate((float)sr);
    g_orbeRouteA.prepare((float)sr);
    g_holosRouteA.prepare((float)sr);
    g_omniRouteA.prepare((float)sr);
    g_hyperRouteA.prepare((float)sr);
    g_neuroLoopRouteA.prepare((float)sr, 128);
    g_neuroBandEqRouteA.prepare((float)sr);
    g_loudnessMeterInit.store(true, std::memory_order_release);
    // El ADE se inicia al final, después de que g_initialized confirme el grafo DSP.
    // El consumidor de snapshots es observabilidad, no el motor. Debe vivir
    // también cuando el usuario selecciona modo manual, para que el siguiente
    // enable no herede una telemetría sin consumidor.
    if (!g_adaptiveSnapshotStarted.exchange(true, std::memory_order_acq_rel)) {
        g_snapshotRunning.store(true, std::memory_order_release);
#if defined(__EXCEPTIONS)
        try {
            g_snapshotThread = std::thread(adaptiveSnapshotLoop);
            LOGI("AdaptiveState snapshot consumer started");
        } catch (...) {
            g_snapshotRunning.store(false, std::memory_order_release);
            g_adaptiveSnapshotStarted.store(false, std::memory_order_release);
            LOGE("AdaptiveState snapshot consumer could not start");
        }
#else
        // Android arm64 compila con -fno-exceptions (CMakeLists.txt): sin try/catch,
        // mismo patrón que g_bridgeThread.
        g_snapshotThread = std::thread(adaptiveSnapshotLoop);
        LOGI("AdaptiveState snapshot consumer started");
#endif
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
            LOGI("Ruta A: sin dataset RIR en /data/adb/ivanna_omega/rir — usando Golden Master Studio BRIR cuando se active RIR");
        }
        g_rirDataset.store(ds, std::memory_order_release);
        auto* conv = new Ivanna::RirConvolver();
        conv->applySofaCoupling(ivanna::master::kMasterSafGoldenQ);
        const float initRt60 = g_localRoomRt60.load(std::memory_order_acquire);
        const float initWet  = g_localRoomWet.load(std::memory_order_acquire);
        conv->synthesizeMasterStudioBrir(initRt60 >= 0.01f ? initRt60 : 0.34f, sr);
        if (initRt60 >= 0.01f && initWet > 0.001f) {
            conv->setWetDry(initWet);
            if (ds && ds->isLoaded() && ds->roomCount() > 0) {
                const int32_t reqIdx = g_localRoomIdx.load(std::memory_order_acquire);
                const int32_t targetIdx = (reqIdx >= 0 && static_cast<size_t>(reqIdx) < ds->roomCount())
                    ? reqIdx
                    : static_cast<int32_t>(ds->findNearestSmart(initRt60));
                g_rirPendingIdx.store(targetIdx, std::memory_order_release);
            }
        } else {
            conv->setWetDry(0.0f);
        }
        g_rirConvolver.store(conv, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> uiLock(g_uiMutex);
        g_params_ui = g_params;
    }
    if (!g_rirWorkerRunning.exchange(true, std::memory_order_acq_rel)) {
        g_rirWorkerThread = std::thread(rirWorkerLoop);
        // NO detach — se une en JNI_OnUnload antes de los destructores.
    }
    {
        ivanna::spatial::IvannaAudioPipeline::setActiveInstance(&g_liveAudioPipeline);
        g_cochlearEngine.prepare(static_cast<float>(sr), 512);
        g_cochlearEngine.setIntensity(g_cochlearIntensity.load(std::memory_order_relaxed));
        g_cochlearEngine.setEnabled(g_cochlearEnabled.load(std::memory_order_relaxed));
        auto& pipe = ivanna::spatial::IvannaAudioPipeline::getActiveInstance();
        pipe.prepare(static_cast<float>(sr), 512);
        // Autoridad única en Ruta A/C: g_cochlearEngine. El motor coclear interno de
        // g_liveAudioPipeline permanece desactivado para evitar doble procesado.
        pipe.cochlearEngine().setIntensity(g_cochlearIntensity.load(std::memory_order_relaxed));
        pipe.cochlearEngine().setEnabled(false);
        pipe.pinnaManifoldInterpolator().calibrateFromLatents(
            ivanna::master::kMasterSafGoldenQNorm[2],
            ivanna::master::kMasterSafGoldenQNorm[3],
            ivanna::master::kMasterSafGoldenQNorm[0],
            (float)sr);
        g_unifiedPipeline.prepare(static_cast<float>(sr), 512);
    }
    // Cadena 4D: se prepara AQUÍ con el sample rate REAL del DSP (idempotente). Antes solo la
    // inicializaba el panel de la UI con 48000/480 fijos: sin abrir el panel, feed/apply eran
    // no-ops y con un dispositivo a 44.1 kHz el conductor temporizaba con un bloque erróneo.
    ivanna::fourd::ensureInit(static_cast<float>(sr), ivanna::fourd::kAnalysisChunk);
    if (g_npeAPreparedSr.load(std::memory_order_relaxed) != static_cast<float>(sr)) {
        g_npeA.prepare(static_cast<float>(sr));   // bajo g_dspProcessMutex: el audio no corre a la vez
        g_npeAPreparedSr.store(static_cast<float>(sr), std::memory_order_relaxed);
    }
    routeAStartEvoEqCalibration();   // hilo propio; el efecto se activa solo cuando la calibracion verifica
    g_initialized.store(true, std::memory_order_release);
    // El ADE solo arranca cuando el grafo nativo ya está preparado. La
    // preferencia puede haberse fijado antes por Application/UI; ensure()
    // la respeta y nunca reactiva el motor si el usuario eligió OFF/manual.
    if (ensureAdaptiveEngineStarted()) {
        LOGI("AdaptiveDecisionEngine running (DSP initialized, control thread @50ms)");
    } else {
        LOGI("AdaptiveDecisionEngine disabled by preference or start unavailable");
    }
    LOGI("OPE initialized @ %d Hz (EvolutionaryKernel online)", sr);
}

// ── FIX CRÍTICO (tronido al mover sliders + aplicar ISO 226) ────────────────
// nativeSetFatigueProtection() hacía `g_params.high += comp` y
// `g_params.mid -= protect*3`: ACUMULATIVO sobre el valor ya vigente. Cada
// recálculo de ISO 226 / fatiga (el calibrador lo emite varias veces por
// segundo mientras el nivel de escucha varía) sumaba otra vez la misma
// compensación → high derivaba hacia +18 dB (tope del clamp del EQ) en pocos
// segundos → biquads en boost extremo + GainStage + SafetyLimiter en
// compresión de pared → golpe digital audible.
//
// Fix: el EQ se deriva SIEMPRE de una base absoluta (lo último que pidió el
// usuario/curva perceptual) más los offsets vigentes de ISO/fatiga. Llamadas
// repetidas con los mismos valores son idempotentes.
static std::atomic<float> g_eqBaseLow {2.2f};
static std::atomic<float> g_eqBaseMid {0.8f};
static std::atomic<float> g_eqBaseHigh{1.8f};
static std::atomic<float> g_fatigueIsoDb  {0.0f};   // compensación ISO 226 (dB, ±12)
static std::atomic<float> g_fatigueProtect{0.0f};   // protección de fatiga [0..1]

// ── Colisión compresor/spatial_width: ThermalGovernor vs decisión IA ────────
// FIX (auditoría FASE 7): nativeSetCompressorAmount (ThermalGovernor, tick
// periódico por temperatura) y nativeSetCompressorParams (DspStateUpdater,
// decisión de IA/usuario) escribían AMBAS, de forma absoluta y sin
// coordinarse, sobre g_comp.threshold/ratio — igual nativeSetSpatialWidth
// (ThermalGovernor) y nativeSetSpatialWidthDirect (IA) sobre
// g_pd.spatial_width. Cada tick de cualquiera de los dos lados pisaba al
// otro; con ambos activos, el parámetro final dependía de quién escribió
// último, no de intención real (mismo tipo de bug ya resuelto para el EQ
// con recomputeEqFromBaseLocked — ver arriba). Se replica ese patrón: el
// lado IA fija una BASE, el lado térmico fija una ESCALA multiplicativa
// [0..1] (que es exactamente lo que ThermalGovernor.kt ya calculaba como
// "1f - cut" antes de esta corrección — su semántica era de escala, solo
// el receptor nativo la trataba como valor absoluto). Térmico nunca
// sustituye la intención de la IA, solo la atenúa; ninguno de los dos
// escritores queda ciego al otro.
static std::atomic<float> g_compBaseThresholdDb{-16.0f};
static std::atomic<float> g_compBaseRatio      {2.4f};
static std::atomic<float> g_thermalCompScale   {1.0f};   // 1 = sin recorte térmico
static std::atomic<float> g_spatialBaseWidth   {1.32f};
static std::atomic<float> g_thermalSpatialScale{1.0f};   // 1 = sin recorte térmico
// Último ancho ENTREGADO al StereoWidener (base × escala térmica, ya acotado). Se publica en el
// mismo punto que lo escribe en g_pd, para que nativeGetSpatialWidthDirect devuelva el estado
// efectivo del procesador y no una copia de lo que pidió la UI.
static std::atomic<float> g_spatialAppliedWidth{1.32f};
// Last finite, clamped spatial-angle command; not an acoustic measurement.
static std::atomic<float> g_spatialAppliedAngleRad{0.0f};

// Requiere g_dspProcessMutex tomado por el llamador (hilo de audio).
static void recomputeCompressorLocked() {
    const float att = g_compPendingAttackMs.exchange(-1.0f, std::memory_order_acq_rel);
    const float rel = g_compPendingReleaseMs.exchange(-1.0f, std::memory_order_acq_rel);
    if (att > 0.0f) g_comp.setAttack(att);
    if (rel > 0.0f) g_comp.setRelease(rel);
    const float scale = std::clamp(g_thermalCompScale.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float baseThr   = g_compBaseThresholdDb.load(std::memory_order_relaxed);
    const float baseRatio = g_compBaseRatio.load(std::memory_order_relaxed);
    g_comp.setThreshold(baseThr * scale);
    g_comp.setRatio(1.0f + (baseRatio - 1.0f) * scale);
}

static void recomputeSpatialWidthLocked() {
    const float scale = std::clamp(g_thermalSpatialScale.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float base  = g_spatialBaseWidth.load(std::memory_order_relaxed);
    // Misma matematica que antes, extraida a supreme/SpatialWidthReadback.hpp para poder
    // verificar en host el contrato del read-back (ver test_spatial_width_readback).
    const float applied = ivanna::supreme::SpatialWidthReadback::applied(base, scale);
    g_pd.set_spatial_width(applied);
    g_spatialAppliedWidth.store(applied, std::memory_order_release);
}

static inline float normalizeMasterToDb(float master) noexcept {
    if (!std::isfinite(master)) return 0.0f;
    // AudioState.masterGain (SoundScreen / DspStateUpdater) vive en [0.1 .. 2.0] lineal.
    // DSPState.master / Iso226Calibrator vive en [-24 .. +18] dB (donde 0.0f = 0 dB unidad).
    if (master >= 0.05f && master <= 2.0f) {
        return std::clamp(20.0f * std::log10(master), -60.0f, 6.0f);
    }
    if (std::fabs(master) < 0.05f) {
        return 0.0f; // 0.0 dB unidad (nunca silenciar a -60 dB cuando DSPState manda 0 dB)
    }
    return std::clamp(master, -60.0f, 6.0f);
}

// Requiere g_dspProcessMutex tomado por el llamador (hilo de audio).
static void recomputeEqFromBaseLocked() {
    const float iso  = g_fatigueIsoDb.load(std::memory_order_relaxed);
    const float prot = g_fatigueProtect.load(std::memory_order_relaxed);
    g_params.low  = std::clamp(g_eqBaseLow.load(std::memory_order_relaxed), -24.0f, 24.0f);
    g_params.mid  = std::clamp(g_eqBaseMid.load(std::memory_order_relaxed) - prot * 3.0f,
                               -24.0f, 24.0f);
    g_params.high = std::clamp(g_eqBaseHigh.load(std::memory_order_relaxed)
                                   + iso * (1.0f - prot), -24.0f, 24.0f);
    g_eq.setParams(g_params);
    const float comp  = g_eq.getOutputCompensationDb();
    const float mDb   = g_params.master;
    g_params.master   = std::clamp(mDb - comp, -60.0f, 6.0f);
    g_gain.setParams(g_params);
    g_params.master   = mDb;
}

static void drainPendingDspParamsLocked() noexcept {
    if (g_params_dirty.exchange(false, std::memory_order_acquire)) {
        if (g_uiMutex.try_lock()) {
            const uint32_t currentSr = g_params.sampleRate;
            g_params = g_params_ui;
            if (g_params.sampleRate == 0 && currentSr > 0) {
                g_params.sampleRate = currentSr;
            }
            g_uiMutex.unlock();

            g_eq.setParams(g_params);
            g_comp.setParams(g_params);
            g_exciter.setParams(g_params);
            g_widener.setParams(g_params);
            const float compNsp = g_eq.getOutputCompensationDb();
            const float masterDbNsp = g_params.master;
            g_params.master = std::clamp(masterDbNsp - compNsp, -60.0f, 6.0f);
            g_gain.setParams(g_params);
            g_params.master = masterDbNsp;

            g_pd.set_nho_alpha(g_params.alpha);
            g_pd.set_nho_beta(g_params.beta);
            g_nho_wet_exciter.store(g_params.wet * 0.5f, std::memory_order_relaxed);
            applyNhoWet();
        } else {
            g_params_dirty.store(true, std::memory_order_release);
        }
    }
    if (g_eq_dirty.exchange(false, std::memory_order_acquire)) {
        if (g_uiMutex.try_lock()) {
            g_params.presence = g_params_ui.presence;
            g_params.master   = g_params_ui.master;
            g_uiMutex.unlock();
        }
        recomputeEqFromBaseLocked();
    }
    if (g_comp_dirty.exchange(false, std::memory_order_acquire)) {
        recomputeCompressorLocked();
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet, jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma_v,
    jfloat freq,  jfloat resonance,
    jfloat low,   jfloat mid,  jfloat high,
    jfloat presence, jfloat master) {
    g_eqBaseLow .store(std::clamp((float)low,  -24.0f, 24.0f), std::memory_order_relaxed);
    g_eqBaseMid .store(std::clamp((float)mid,  -24.0f, 24.0f), std::memory_order_relaxed);
    g_eqBaseHigh.store(std::clamp((float)high, -24.0f, 24.0f), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_uiMutex);
    if (g_params_ui.sampleRate == 0) g_params_ui = g_params;
    g_params_ui.drive = drive; g_params_ui.wet = wet;   g_params_ui.mix = mix;
    g_params_ui.alpha = alpha; g_params_ui.beta = beta; g_params_ui.gamma = gamma_v;
    g_params_ui.freq  = freq;  g_params_ui.resonance = resonance;
    g_params_ui.low   = low;   g_params_ui.mid = mid;   g_params_ui.high = high;
    g_params_ui.presence = presence;
    const float masterDbNsp = normalizeMasterToDb(master);
    g_params_ui.master = masterDbNsp;
    g_params_dirty.store(true, std::memory_order_release);
    // NHO parameters mapped from DSP params (suavizados internamente por muestra)
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
    if (!std::isfinite(score)) return;
    g_voice_protect_score.store(
        std::clamp(score, 0.f, 1.f), std::memory_order_relaxed);
    // Este setter solo es invocado después de una inferencia YAMNet válida.
    g_voice_protect_score_valid.store(true, std::memory_order_release);
}

// Reset de procedencia para el cambio a modo automático, manual OFF o release.
// No reinicia EQ, compresor, spatial DSP ni el resto del grafo.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeClearVoiceProtectScore(JNIEnv*, jobject) {
    g_voice_protect_score.store(0.0f, std::memory_order_relaxed);
    g_voice_protect_score_valid.store(false, std::memory_order_release);
}

// ── REINYECCION DIFERENCIAL DE RUTA A (v2.5.0) ─────────────────────────────
// Reemplaza el delta (procesado - seco) en Kotlin: alinea el seco con el procesado
// (latencia propia del DSP, fraccional), mide la coherencia delta<->seco al retardo L
// del oyente por banda y limita la ganancia para que el peine quede < ~0.8 dB.
// Un solo hilo de captura llama a process(); los setters son atomics (cualquier hilo).
static ivanna::DifferentialReinjector g_reinjector;
static std::atomic<bool> g_reinjectorReady{false};

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectPrepare(JNIEnv*, jobject, jint sampleRate, jint maxFrames) {
    g_reinjectorReady.store(false, std::memory_order_release);   // fuera de RT: reserva memoria aqui
    g_reinjector.prepare(static_cast<float>(sampleRate), static_cast<int>(maxFrames));
    g_reinjectorReady.store(true, std::memory_order_release);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectReset(JNIEnv*, jobject) {
    if (g_reinjectorReady.load(std::memory_order_acquire)) g_reinjector.reset();
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectSetIntensity(JNIEnv*, jobject, jfloat v) {
    g_reinjector.setIntensity(v);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectSetLatencyMs(JNIEnv*, jobject, jfloat ms) {
    g_reinjector.setListenerLatencyMs(ms);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectSetBandCaps(JNIEnv*, jobject, jfloat lo, jfloat mid, jfloat hi) {
    g_reinjector.setBandCaps(lo, mid, hi);
}
// dry/wet/out: estereo intercalado, mismo tamano. Devuelve false si no esta listo (el llamador cae al delta simple).
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectProcess(
    JNIEnv* env, jobject, jfloatArray dry, jfloatArray wet, jfloatArray out, jint nFrames) {
    if (!g_reinjectorReady.load(std::memory_order_acquire) || !dry || !wet || !out || nFrames <= 0) return JNI_FALSE;
    const int n = std::min({(int)nFrames, 2048, (int)(env->GetArrayLength(dry) / 2),
                            (int)(env->GetArrayLength(wet) / 2), (int)(env->GetArrayLength(out) / 2)});
    if (n <= 0) return JNI_FALSE;
    jfloat* d = env->GetFloatArrayElements(dry, nullptr);
    jfloat* w = env->GetFloatArrayElements(wet, nullptr);
    jfloat* o = env->GetFloatArrayElements(out, nullptr);
    if (d && w && o) {
        // Voz detectada (VoiceProtectionController) => el techo de inmersion baja (inteligibilidad primero).
        g_reinjector.setContentHint(g_voice_protect_score.load(std::memory_order_relaxed), 0.f);
        g_reinjector.process(d, w, o, n);
    }
    if (d) env->ReleaseFloatArrayElements(dry, d, JNI_ABORT);
    if (w) env->ReleaseFloatArrayElements(wet, w, JNI_ABORT);
    if (o) env->ReleaseFloatArrayElements(out, o, 0);
    return (d && w && o) ? JNI_TRUE : JNI_FALSE;
}
// Telemetria real (no constantes): [latMs, dspLag, lagConf, coh0..2, ratio0..2, gain0..2, combRisk, immersion, ceiling] = 16 floats.
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReinjectTelemetry(JNIEnv* env, jobject, jfloatArray out) {
    if (!out || env->GetArrayLength(out) < 16) return 0;
    const auto t = g_reinjector.telemetry();
    float v[16] = {t.listenerLatencyMs, t.dspLagSamples, t.lagConfidence,
                   t.corr[0], t.corr[1], t.corr[2], t.deltaToDry[0], t.deltaToDry[1], t.deltaToDry[2],
                   t.gain[0], t.gain[1], t.gain[2], t.combRisk, t.immersion, t.immersionCeiling, 0.f};
    env->SetFloatArrayRegion(out, 0, 16, v);
    return 15;
}

struct NonBlockingDspProcessGuard {
    bool acquired;
    NonBlockingDspProcessGuard() noexcept : acquired(g_dspProcessMutex.try_lock()) {}
    ~NonBlockingDspProcessGuard() noexcept {
        if (acquired) g_dspProcessMutex.unlock();
    }
};

// ── Gobernador de shed de carga DSP (Ruta A, v2.5.375) ────────────────────
// Medido en vivo (Moto G85, 2026-11-09): dsp_bridge 3.5–8.9 ms por bloque
// de 320 frames @48 kHz frente a un periodo de 6.67 ms — carga sostenida
// 100–105% que drena el anillo de AudioRecord y desborda el RecordThread.
// Solo se toca bajo g_dspProcessMutex, en el hilo de audio de Ruta A
// (un solo llamador de nativeProcess); ver dsp/dsp_load_shed_governor.hpp.
static ivanna::dsp::DspLoadShedGovernor g_dspShedGovernor;

// Log throttled del shed (max 1 vez / 5 s mientras hay shed activo, y una
// vez al terminar): visible en logcat para verificar en el telefono sin
// inundar el log — un LOGI por bloque seria ~150 lineas/s.
static void logDspShedStateThrottled() noexcept {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point lastLog{};
    static uint64_t lastLoggedSheds = 0;
    static bool lastLoggedShedding = false;
    const uint64_t sheds = g_dspShedGovernor.shedBlocks();
    const bool shedding = g_dspShedGovernor.debtUs() > 0;
    if (sheds == lastLoggedSheds && shedding == lastLoggedShedding) return;
    const auto now = Clock::now();
    // FIX v2.5.410 — la recuperación ("terminado") NO se throttleaba:
    // cada bloque en shed incrementa `sheds`, así que cada transición
    // shed→recuperación imprimía una línea: 2836 líneas en 7 min (~7/s)
    // desde el hilo de audio RT en la captura 2026-11-11 19:09 (y las
    // líneas ACTIVO quedaban ahogadas por el mismo throttle: 0 en 7 min).
    // Ahora AMBOS estados se limitan a 1 línea / 5 s una vez que ya se
    // logueó (el primer cambio de estado siempre se anuncia). Los
    // contadores son acumulativos, así que cada línea sigue mostrando los
    // totales reales; el detalle por segundo vive en IVANNA.Telemetry
    // (campo shed=, v2.5.410) sin loguear en RT.
    if (lastLoggedSheds != 0 && now - lastLog < std::chrono::seconds(5)) return;
    lastLog = now;
    lastLoggedSheds = sheds;
    lastLoggedShedding = shedding;
    if (shedding) {
        LOGI("DSP shed Ruta A ACTIVO: %llu/%llu bloques en passthrough (%.1f %%), EMA DSP %lld us, pico %lld us — el DSP no alcanza el periodo; se degrada con gracia (seco en vivo, sin silencio)",
             static_cast<unsigned long long>(sheds),
             static_cast<unsigned long long>(g_dspShedGovernor.totalBlocks()),
             g_dspShedGovernor.shedFraction() * 100.0,
             static_cast<long long>(g_dspShedGovernor.emaUs()),
             static_cast<long long>(g_dspShedGovernor.peakUs()));
    } else {
        LOGI("DSP shed Ruta A terminado: %llu/%llu bloques en passthrough (%.1f %%), EMA DSP %lld us — el bucle vuelve a procesar todos los bloques",
             static_cast<unsigned long long>(sheds),
             static_cast<unsigned long long>(g_dspShedGovernor.totalBlocks()),
             g_dspShedGovernor.shedFraction() * 100.0,
             static_cast<long long>(g_dspShedGovernor.emaUs()));
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess(
    JNIEnv* env, jobject, jfloatArray buf, jint nFrames) {
    NonBlockingDspProcessGuard dspGuard;
    if (!dspGuard.acquired) return;
    drainPendingDspParamsLocked();
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
    // Buffer intercalado L/R: n frames exigen 2n floats. Sin este límite un array
    // más corto que nFrames provocaba lectura/escritura fuera de rango del heap.
    const int n = std::min({(int)nFrames, 2048, (int)(env->GetArrayLength(buf) / 2)});
    if (n <= 0) return;
    // ── Shed de carga DSP (v2.5.375; presupuesto 85% desde v2.5.410) ───────
    // Si la cadena viene debiendo tiempo (coste medio > 85% del periodo),
    // este bloque va en SECO: se retorna SIN tocar `buf` — exactamente el
    // mismo passthrough que ya ocurre cuando el mutex esta tomado o el DSP
    // no esta inicializado. Ningun estado interno avanza a medias, la
    // senal original sigue sonando y la reinyeccion diferencial aporta ~0.
    // Se decide ANTES de GetFloatArrayElements para no pagar ni la copia JNI.
    const int64_t periodUs = (g_params.sampleRate > 0)
        ? static_cast<int64_t>(n) * 1000000LL / static_cast<int64_t>(g_params.sampleRate)
        : 0;
    if (g_dspShedGovernor.shouldShed(periodUs)) {
        g_dspShedGovernor.observeShed(periodUs);
        logDspShedStateThrottled();
        return;
    }
    const auto dspT0 = std::chrono::steady_clock::now();
    jfloat* data = env->GetFloatArrayElements(buf, nullptr);
    if (!data) return;
    // Alimentar MusicIntelligenceEngine (IME) con el bloque estéreo intercalado seco
    // (RT-safe: verifica atómicamente enabled, cero malloc, seqlock).
    ivanna::ime::imeFeedBlock(data, n);
    // Cadena 4D (v2.5.48): se alimenta con la señal SECA, antes de la cadena DSP, porque la
    // predicción de golpes necesita los transitorios sin procesar. Apagada por defecto: con la
    // cadena desactivada ambas llamadas retornan sin tocar un solo bit ni reservar memoria.
    ivanna::fourd::feedInterleaved(data, n, 2);
    ivanna::fourd::publishAnticipatedScene();
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
    // ── v2.5.15 · ACOUSTIC UNITY ENGINE en Ruta A ────────────────────────────
    // Hasta v2.5.14 solo corría en Ruta B (audioserver); la ruta que usa la app no lo llamaba.
    // Lee el ORIGINAL (pre-cadena) y publica el contexto/presupuestos que se consumen más abajo.
    // tonalityHint = 0: Ruta A no mide tonalidad (no hay motor de prosodia en este proceso), así que no
    // se inventa; la escena "música audiófila" solo existe donde hay medición (Ruta B).
    auto& unityEngine = ivanna::unity::AcousticUnityEngine::instance();
    unityEngine.setEnabled(g_unity_enabled.load(std::memory_order_relaxed));
    unityEngine.setSampleRate(static_cast<float>(g_params.sampleRate));
    unityEngine.coordinateAcousticOrganism(
        g_ats.dryL, g_ats.dryR, static_cast<size_t>(n),
        g_voice_protect_score.load(std::memory_order_relaxed), 0.0f,
        g_upmixing_enabled.load(std::memory_order_relaxed),
        routeAWfsAllowed(),
        g_rirConvolver.load(std::memory_order_acquire) != nullptr,
        g_params.drive);
    const ivanna::unity::AcousticUnityContext unityCtx = unityEngine.getContext();
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
    // Pre-EQ peak guard: IIR biquads divergen si reciben >~1.0.
    // mix=0.8 de ISO226 añade +3.6dB antes del EQ; biquad AGUDOS +8.4dB
    // acumula estado → NaN → SafetyLimiter clampea a 0 → tronido.
    // Reducción por bloque completo (sin AM per-sample = sin distorsión).
    {
        float pk = 0.f;
        for (int i = 0; i < n; ++i) {
            const float al = g_ats.chL[i] < 0.f ? -g_ats.chL[i] : g_ats.chL[i];
            const float ar = g_ats.chR[i] < 0.f ? -g_ats.chR[i] : g_ats.chR[i];
            if (al > pk) pk = al;
            if (ar > pk) pk = ar;
        }
        // FIX (tronido "metralleta" al subir volumen, verificado contra el
        // código real antes de tocarlo): este guard aplicaba sc=0.89/pk al
        // bloque COMPLETO de golpe, sin memoria del bloque anterior. Con la
        // señal rondando el umbral — justo lo que pasa al subir volumen
        // hacia niveles donde los picos empiezan a rozar el techo — sc
        // alternaba 1.0<->reducido de un bloque al siguiente: cada cambio
        // es un salto de ganancia instantáneo. GainStage ya tiene rampa
        // propia para su multiplicador; este guard, separado, no tenía
        // ninguna.
        //
        // Ataque instantáneo, release en rampa (diseño estándar de
        // limitadores, no una rampa simétrica): este guard existe para que
        // el EQ nunca reciba >~1.0 y diverja — es seguridad, no estética,
        // así que la reducción se aplica COMPLETA de inmediato en cuanto
        // hace falta (idéntico al comportamiento original: cero regresión
        // en la protección). Rampear el ataque dejaría justo la muestra
        // que causó el pico con menos reducción de la necesaria mientras
        // la rampa se pone al día — inaceptable para un guard de
        // seguridad. Al soltar (targetSc > startSc) sí se rampea muestra
        // a muestra: ahí no hay riesgo de divergencia (siempre se viene
        // de una reducción hacia menos reducción), y es exactamente donde
        // vivía el escalón audible al bajar la protección de golpe.
        // FIX (crujido/rugosidad constante, causa raiz): el umbral 0.89 se superaba en casi
        // cualquier master moderno (picos 0.95-1.0) -> targetSc = 0.89/pk cambiaba CADA bloque
        // (~150 Hz) con ataque instantaneo = modulacion de amplitud a nivel de bloque. Un biquad
        // IIR estable no diverge por 1.0-1.5; el SafetyLimiter final ya fija el techo. Este guard
        // queda solo como proteccion de emergencia (entrada > +3.5 dBFS).
        constexpr float kPreEqGuard = 1.5f;
        const float targetSc = (pk > kPreEqGuard && pk > 1e-9f) ? (kPreEqGuard / pk) : 1.0f;
        const float startSc = g_ats.peakGuardScale;
        if (targetSc < startSc) {
            for (int i = 0; i < n; ++i) { g_ats.chL[i] *= targetSc; g_ats.chR[i] *= targetSc; }
            g_ats.peakGuardScale = targetSc;
        } else if (targetSc > startSc) {
            for (int i = 0; i < n; ++i) {
                const float t = (n > 1) ? (float)i / (float)(n - 1) : 1.0f;
                const float sc = startSc + (targetSc - startSc) * t;
                g_ats.chL[i] *= sc;
                g_ats.chR[i] *= sc;
            }
            g_ats.peakGuardScale = targetSc;
        }
    }
    // v2.5.15: eq_tilt_db (atenuación de fatiga en agudos) del ADE: calculado y publicado, nunca aplicado.
    g_eq.setFatigueTilt(blend_adaptive_from_neutral(
        0.0f, std::clamp(g_lastAdaptiveEqTiltDb.load(std::memory_order_relaxed), -3.0f, 0.0f),
        adaptive_ui_strength()));
    g_eq.process(g_ats.chL, g_ats.chR, n);
    routeAApplyEvoEq(g_ats.chL, g_ats.chR, n);   // paridad con Ruta B (EQ evolutivo 512 bandas)
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
    const bool adaptiveActive = adaptiveStrength > 0.0f;
    const float targetGainUi = blend_adaptive_from_neutral(
        1.0f,
        std::clamp(g_lastAdaptiveTargetGain.load(std::memory_order_relaxed), 0.5f, 1.0f),
        adaptiveStrength);
    // v2.5.15: la compresión ADICIONAL sugerida cede ante la pérdida de cresta MEDIDA (dynamicsBudget) y la
    // reducción del exciter sube si la salida medida gana demasiado agudo (harmonicBudget). El piso de
    // seguridad del Perceptual Guard (más abajo) se aplica DESPUÉS y sigue mandando.
    const float compAmountUi = unityCtx.dynamicsBudget * blend_adaptive_from_neutral(
        0.0f,
        std::clamp(g_lastAdaptiveCompAmount.load(std::memory_order_relaxed), 0.f, 1.f),
        adaptiveStrength);
    const float excReductionUi = std::max(1.0f - unityCtx.harmonicBudget, blend_adaptive_from_neutral(
        0.0f,
        std::clamp(g_lastAdaptiveExcReduction.load(std::memory_order_relaxed), 0.f, 1.f),
        adaptiveStrength));
    if (!adaptiveActive) {
        // OFF/manual: neutralize the ADE contribution while preserving the
        // independent measured harmonic budget and the Perceptual Guard target.
        // Compressor user threshold/ratio/attack/release remain untouched; its
        // runtime overlay slews toward the guard's current request below.
        g_ats.targetGainSmooth = 1.0f;
        g_ats.compAmountSmooth = 0.0f;
        g_ats.excReductionSmooth = std::clamp(excReductionUi, 0.0f, 1.0f);
        g_gain.setRuntimeGain(1.0f);
    } else {
        g_ats.targetGainSmooth += 0.05f * (targetGainUi - g_ats.targetGainSmooth);
        g_ats.compAmountSmooth += 0.05f * (compAmountUi - g_ats.compAmountSmooth);
        g_ats.excReductionSmooth += 0.05f * (excReductionUi - g_ats.excReductionSmooth);
    }
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
    g_crossfeed.process(g_ats.chL, g_ats.chR, n);   // despues del widener y antes de la ganancia de salida/limiter
    // ORBE (Ruta A): etapa de campo cercano tras el crossfeed, antes de la ganancia
    // de salida. Sin HOA en proceso en esta ruta, ORBE aplica aquí directamente.
    // ORBE + HOLOS EN SERIE (v2.5.369, directiva explícita): AMBOS activos —
    // ORBE primero, HOLOS después (ver bloque abajo). Lee los atomics
    // g_holos_*/g_orbe_* cada bloque; amount=0 es bypass bit-exacto de
    // coste cero. En serie el retardo algorítmico se suma: 512 muestras de
    // ORBE + 512 de HOLOS ≈ 21.3 ms @48 kHz (más 31 de IVANNA).
    // Bucle cerrado neuro-acústico (v2.5.365): 1x por bloque, ANTES de
    // ORBE + HOLOS en serie (v2.5.369, directiva explícita): AMBOS activos.
    // Antes era if/else (HOLOS ocupaba el lugar de ORBE); con ambos defaults
    // > 0, ORBE quedaba muerto. ORBE primero, HOLOS después. Cada uno es
    // bypass bit-exacto si su amount=0.
    // HOLOS/ORBE — NeuroBandEq + deltas acotados sobre hp/op. Apagado o
    // sin frames frescos -> identidad bit-exacta.
    const auto neuroAdjA = neuroLoopRouteAUpdate(
        g_ats.chL, g_ats.chR, n,
        static_cast<float>(std::max(g_params.sampleRate, 8000u)));
    {
        // ORBE primero.
        const float orbeAmount = g_orbe_amount.load(std::memory_order_relaxed);
        if (orbeAmount > 0.0f) {
            ivanna::OrbeParams op;
            op.amount = orbeAmount;
            op.distanceM = g_orbe_distance_m.load(std::memory_order_relaxed);
            op.spread = g_orbe_spread.load(std::memory_order_relaxed);
            op.proximityDb = g_orbe_proximity_db.load(std::memory_order_relaxed);
            op.diffuse = g_orbe_diffuse.load(std::memory_order_relaxed);
            op.headTracking = g_orbe_head_tracking.load(std::memory_order_relaxed);
            op.headRadiusM = g_orbe_head_radius_m.load(std::memory_order_relaxed);
            op.bandCount = 24;
            {   // Bucle cerrado: deltas locales sobre la base del usuario.
                const auto tgt = neuroLoopTargetsOrbe(neuroAdjA);
                op.distanceM = tgt.orbeDistanceM;
                op.proximityDb = tgt.orbeProximityDb;
                op.diffuse = tgt.holosDiffuse;
            }
            g_orbeRouteA.setThermalShed(g_spatialShedA.load(std::memory_order_relaxed));
            g_orbeRouteA.setParams(op);
            g_orbeRouteA.process(g_ats.chL, g_ats.chR, n);
        }
        // HOLOS después.
        const float holosAmount = g_holos_amount.load(std::memory_order_relaxed);
        if (holosAmount > 0.0f) {
            ivanna::HolosParams hp;
            hp.amount = holosAmount;
            hp.distanceM = g_holos_distance_m.load(std::memory_order_relaxed);
            hp.spread = g_holos_spread.load(std::memory_order_relaxed);
            hp.proximityDb = g_holos_proximity_db.load(std::memory_order_relaxed);
            hp.diffuse = g_holos_diffuse.load(std::memory_order_relaxed);
            hp.harmonics = std::clamp(g_holos_harmonics.load(std::memory_order_relaxed), 0.0f, ivanna::kHolosHarmonicsMaxValidated);  // guarda v2.5.411: el valor guardado (p. ej. 0.298) nunca llega al DSP sin validar
            hp.warmth = g_holos_warmth.load(std::memory_order_relaxed);
            hp.dynamics = g_holos_dynamics.load(std::memory_order_relaxed);
            hp.width = g_holos_width.load(std::memory_order_relaxed);
            hp.gamma = g_holos_gamma.load(std::memory_order_relaxed);
            hp.roomRt60 = g_holos_room_rt60.load(std::memory_order_relaxed);
            hp.headTracking = g_holos_head_tracking.load(std::memory_order_relaxed);
            hp.eqLow = g_holos_eq_low.load(std::memory_order_relaxed);
            hp.eqMid = g_holos_eq_mid.load(std::memory_order_relaxed);
            hp.eqHigh = g_holos_eq_high.load(std::memory_order_relaxed);
            hp.eqPresence = g_holos_eq_presence.load(std::memory_order_relaxed);
            hp.eqTiltDb = g_holos_eq_tilt_db.load(std::memory_order_relaxed);
            hp.eqFreq = g_holos_eq_freq.load(std::memory_order_relaxed);
            hp.bandCount = (g_holos_band_count.load(std::memory_order_relaxed) == 12) ? 12 : 24;
            {   // Bucle cerrado: deltas locales sobre la base del usuario.
                const auto tgt = neuroLoopTargetsHolos(neuroAdjA);
                hp.gamma = tgt.holosGamma;
                hp.width = tgt.holosWidth;
                hp.diffuse = tgt.holosDiffuse;
                hp.distanceM = tgt.orbeDistanceM;
                hp.proximityDb = tgt.orbeProximityDb;
            }
            g_holosRouteA.setThermalTier(static_cast<ivanna::HolosThermalTier>(
                std::clamp(g_holosTierA.load(std::memory_order_relaxed), 0, 3)));
            g_holosRouteA.setParams(hp);
            g_holosRouteA.process(g_ats.chL, g_ats.chR, n);
        }
    }
    // IVANNA (v2.5.375): tras HOLOS, antes de Φ (ver omniProcessRouteA).
    omniProcessRouteA(g_ats.chL, g_ats.chR, n);
    // Hiper-Operador Φ (v2.5.369): tras HOLOS/ORBE, antes de la ganancia
    // de salida y del limiter. amount=0 = no-op.
    hyperProcessRouteA(g_ats.chL, g_ats.chR, n,
                       static_cast<float>(std::max(g_params.sampleRate, 8000u)));
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
        const float sp_angle = std::clamp((g_control_frame.evo_genome_spatial[0].load(std::memory_order_relaxed) - 0.5f) * 60.f, -30.f, 30.f);
        const float sp_width = std::clamp(g_control_frame.evo_genome_spatial[1].load(std::memory_order_relaxed) * 1.5f, 0.f, 1.5f);
        g_pd.set_nho_alpha(nho_a);
        g_pd.set_nho_beta(nho_b);
        g_pd.set_nho_harmonic(nho_h);
        g_pd.set_spatial_angle(sp_angle);
        g_pd.set_spatial_width(sp_width);
    }
    // buffers en g_ats
    g_pd.process_block(g_ats.chL, g_ats.chR, g_ats.pdOutL, g_ats.pdOutR, n);
    // WFS real en Ruta A, post-PDEngine y pre-RIR/limitador final.
    processRouteAWfs(g_ats.pdOutL, g_ats.pdOutR, n);
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
        const float rawTrimLin = (g_loudness_trim_enabled.load(std::memory_order_relaxed) && std::fabs(trim) > 0.15f)
            ? std::pow(10.f, std::clamp(trim, -12.f, 12.f) / 20.f)
            : 1.0f;
        // FIX (headroom guard para LUFS auto-trim): impedir que el normalizador
        // de sonoridad (-14 LUFS) empuje señales con alto factor de cresta por
        // encima de -1.0 dBFS (0.891 lineal) hacia el SafetyLimiter.
        const float lastPk = std::max(g_lastRawPeak.load(std::memory_order_relaxed), 0.1f);
        const float maxSafeBoostLin = std::max(1.0f, 0.891f / lastPk);
        const float targetTrimLin = std::min(rawTrimLin, maxSafeBoostLin);
        if (std::fabs(targetTrimLin - 1.0f) > 1e-4f || std::fabs(g_ats.loudnessTrimSmooth - 1.0f) > 1e-4f) {
            for (int i = 0; i < n; ++i) {
                g_ats.loudnessTrimSmooth += 0.001f * (targetTrimLin - g_ats.loudnessTrimSmooth);
                g_ats.pdOutL[i] *= g_ats.loudnessTrimSmooth;
                g_ats.pdOutR[i] *= g_ats.loudnessTrimSmooth;
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
        rawM.band_energy_valid = true;
        rawM.gain_reduction_db = grDb;
        rawM.voice_score       = vpScore;
        rawM.voice_score_valid = g_voice_protect_score_valid.load(std::memory_order_acquire);
        rawM.crest_factor_db   = (rms > 1e-6f) ? 20.0f * std::log10(std::max(peakAbs / rms, 1.0f)) : 0.0f;
        rawM.upmix_active      = g_upmixing_enabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        rawM.golden_ear_active = (g_params.drive > 0.05f) ? 1.0f : 0.0f;
        rawM.wfs_active        = routeAWfsAllowed() ? 1.0f : 0.0f;
        rawM.rir_active        = (g_rirConvolver.load(std::memory_order_acquire) != nullptr) ? 1.0f : 0.0f;
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
            g_lastAdaptiveRirWetScale   .store(st.rir_wet_scale,         std::memory_order_relaxed);
            g_lastAdaptiveMsWidenerScale.store(st.ms_widener_scale,      std::memory_order_relaxed);
            g_lastAdaptiveEqTiltDb      .store(st.eq_tilt_db,            std::memory_order_relaxed);
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
        //    ARBITRAJE ESPACIAL: Si HOA Upmixing o WFS están activos, el ensanchador M/S
        //    se neutraliza para respetar la síntesis física/virtual del escenario acústico.
        const float widthTarget = blend_adaptive_from_neutral(
            1.0f,
            std::clamp(g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed), 0.f, 1.5f),
            adaptiveStrength);
        g_ats.widthSmooth += 0.02f * (widthTarget - g_ats.widthSmooth);  // ~50 bloques a τ
        const float adaptiveWidenAmount = std::max(0.f, g_ats.widthSmooth - 1.f);
        // v2.5.15: tercer solicitante (Unity: fuente estrecha y sin voz) y permiso espacial. El permiso es
        // el MÁS CONSERVADOR entre la escala del ADE (voz => 0.25) y el presupuesto MEDIDO sobre la salida
        // real (sobre-expansión / pérdida de correlación): nadie compone, el más estricto manda.
        const float unityWidenAsk = std::max(0.f, unityCtx.msWidenerMultiplier - 1.f);
        const float widenPermission = std::min(
            blend_adaptive_from_neutral(1.0f,
                std::clamp(g_lastAdaptiveMsWidenerScale.load(std::memory_order_relaxed), 0.f, 1.f),
                adaptiveStrength),
            unityCtx.spatialBudget);
        const float combinedWidenAmount =
            std::max({widenAmountFromCorrelation, adaptiveWidenAmount, unityWidenAsk}) * widenPermission;
        const bool spatialHierarchyActive = (g_upmixing_enabled.load(std::memory_order_relaxed) ||
                                             routeAWfsAllowed());
        const float targetWidenAmount = spatialHierarchyActive ? 0.0f : combinedWidenAmount;
        static float s_effectiveWidenSmooth = 0.0f;
        if (targetWidenAmount > 0.001f || s_effectiveWidenSmooth > 0.001f) {
            for (int i = 0; i < n; ++i) {
                s_effectiveWidenSmooth += 0.002f * (targetWidenAmount - s_effectiveWidenSmooth);
                const float sideMul = 1.f + s_effectiveWidenSmooth;
                const float mid  = (g_ats.pdOutL[i] + g_ats.pdOutR[i]) * 0.5f;
                const float side = (g_ats.pdOutL[i] - g_ats.pdOutR[i]) * 0.5f * sideMul;
                g_ats.pdOutL[i] = mid + side;
                g_ats.pdOutR[i] = mid - side;
            }
        } else {
            s_effectiveWidenSmooth = 0.0f;
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
        // v2.5.15: wet BASE pedida por el usuario/snapshot. Antes setWetDry() solo se llamaba cuando llegaba
        // una generación nueva del snapshot, así que ningún factor por bloque podía actuar sobre la sala.
        static float s_rirWetBase = 0.0f;
        ivanna::OmegaDspSnapshot rirSnap{};
        if (ivanna::effectControlBus().readLatest(rirSnap, s_rirLastGen)) {
            auto& realityOrch = ivanna::reality::AcousticRealityOrchestrator::instance();
            if (realityOrch.isEnabled()) {
                realityOrch.coordinateSnapshot(rirSnap);
            }
            const float rt60 = rirSnap.room_rt60_s;
            const float wet  = rirSnap.room_wet;
            if (rt60 < 0.01f || wet <= 0.001f) {
                s_rirWetBase = 0.0f;
                s_rirConvolver->setWetDry(0.f);
                s_rirConvolver->unload();
            } else if (!s_rirDataset || s_rirDataset->roomCount() == 0) {
                s_rirWetBase = wet;
                s_rirConvolver->setWetDry(wet);
                if (s_rirConvolver->needsMasterStudioBrirSynthesis(rt60, (int)g_params.sampleRate)) {
                    g_rirPendingSynthRt60.store(rt60, std::memory_order_release);
                    g_rirWorkerCv.notify_one();
                }
            } else {
                static int32_t s_rirLastIdx = -1;
                const size_t roomIdx = (rirSnap.room_idx >= 0 && static_cast<size_t>(rirSnap.room_idx) < s_rirDataset->roomCount())
                    ? static_cast<size_t>(rirSnap.room_idx)
                    : s_rirDataset->findNearestSmart(rt60);
                if ((int32_t)roomIdx != s_rirLastIdx || !s_rirConvolver->isLoaded()) {
                    // FIX RT: NO leer disco aquí. Publicar el índice y
                    // despertar al worker — él hace WAV+FFT y entrega vía
                    // load() (crossfade incluido). La sala entra ~1-5 ms
                    // después, sin tronido ni XRun.
                    g_rirPendingIdx.store((int32_t)roomIdx, std::memory_order_release);
                    g_rirWorkerCv.notify_one();
                    s_rirLastIdx = (int32_t)roomIdx;
                }
                s_rirWetBase = wet;
                s_rirConvolver->setWetDry(wet);
            }
        }
        // v2.5.15: permiso de sala por bloque = el MÁS CONSERVADOR entre (a) la escala del ADE (voz => 0.65),
        // (b) el duck de voz del Unity Engine (solo con escena de diálogo: es lo único que Ruta A mide) y
        // (c) el presupuesto de sala MEDIDO (EXP/PHASE/CLAR sobre la salida real). Sin componer factores.
        if (s_rirWetBase > 0.001f) {
            const float unityDuck =
                (unityCtx.scene == ivanna::unity::DominantAcousticScene::DialogueVocal)
                    ? unityCtx.rirWetDuckFactor : 1.0f;
            const float rirPermission = std::min({
                blend_adaptive_from_neutral(1.0f,
                    std::clamp(g_lastAdaptiveRirWetScale.load(std::memory_order_relaxed), 0.f, 1.f),
                    adaptiveStrength),
                unityDuck, unityCtx.roomReverbBudget});
            s_rirConvolver->setWetDry(s_rirWetBase * rirPermission);
        }
        if (s_rirConvolver->isLoaded()) {
            const bool extActive = (s_rirConvolver->wetDry() > 0.001f);
            g_liveAudioPipeline.setExternalRirActive(extActive);
            s_rirConvolver->process(g_ats.pdOutL, g_ats.pdOutR, n);
        } else {
            g_liveAudioPipeline.setExternalRirActive(false);
        }
        } // !s_rirConvolver guard
    }
    // ── Supremacía Acústica: Volterra H2 (Anti-Lossy Reconstruction) ──
    if (g_volterra_enabled.load(std::memory_order_relaxed)) {
        static thread_local float voltInter[2 * 2048];
        const int samples = std::min(n, 2048);
        for (int i = 0; i < samples; ++i) {
            voltInter[2 * i]     = g_ats.pdOutL[i];
            voltInter[2 * i + 1] = g_ats.pdOutR[i];
        }
        g_volterra_engine.processInterleaved(voltInter, voltInter, (uint32_t)samples, 2);
        for (int i = 0; i < samples; ++i) {
            g_ats.pdOutL[i] = voltInter[2 * i];
            g_ats.pdOutR[i] = voltInter[2 * i + 1];
        }
    }

    // ── Eje Supremo: Inversión Biomecánica Coclear Activa + Cadena Declarativa + 5 Ejes de Supremacía (Ruta A / Ruta C) ──
    {
        static ivanna::supreme::SupremeAcousticStabilityGuard s_routeAGuard{};
        auto& pipe = ivanna::spatial::IvannaAudioPipeline::getActiveInstance();
        const float srNow = static_cast<float>(std::max(g_params.sampleRate, 8000u));
        const size_t nSamples = static_cast<size_t>(n);

        s_routeAGuard.beginBlock(g_ats.pdOutL, g_ats.pdOutR, nSamples, false);

        const auto unifiedSnap = ivanna::unified::UnifiedParamSnapshotBus::instance().readOncePreLoop();
        const bool cochFlag = g_cochlearEnabled.load(std::memory_order_relaxed) || unifiedSnap.cochlearEnabled;
        // Regla 1.2 (Exclusión mutua de familia Cochlear): si activeCochlearVariant == 1
        // (NeuroCochlearManifold), se desactiva g_cochlearEngine para que corra únicamente B.
        g_cochlearEngine.setIntensity(g_cochlearIntensity.load(std::memory_order_relaxed));
        g_cochlearEngine.setEnabled(cochFlag && (unifiedSnap.activeCochlearVariant == 0u));
        g_cochlearEngine.process(g_ats.pdOutL, g_ats.pdOutR, n);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::CochlearInverse,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.18f, 0.95f);

        auto localUnifiedSnap = unifiedSnap;
        localUnifiedSnap.setStageEnabled(ivanna::unified::StageId::CochlearPinn, false);
        g_unifiedPipeline.syncFromSnapshotPreLoop(localUnifiedSnap);
        g_unifiedPipeline.process(g_ats.pdOutL, g_ats.pdOutR, nSamples, /*syncFromBusPreLoop=*/false);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::NeuroCochlearManifold,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.18f, 0.95f);

        const bool allowLiveSpatial =
            !g_upmixing_enabled.load(std::memory_order_relaxed) &&
            !routeAWfsAllowed();
        ivanna::ime::imeFeedPlanar(g_ats.pdOutL, g_ats.pdOutR, n, srNow);
        ivanna_pilstm_bridge_tick_block(g_ats.pdOutL, g_ats.pdOutR, n, srNow);
        pipe.processLiveSpatialAxes(
            g_ats.pdOutL, g_ats.pdOutR, nSamples, srNow, allowLiveSpatial, 0.16f,
            /*runHearingStage=*/true, /*runCochlearStage=*/false);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::ObjectRenderer,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.20f, 0.95f);

        pipe.snnNmfHoaUpmixer().process(g_ats.pdOutL, g_ats.pdOutR, nSamples);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis3_SnnNmfHoa,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.18f, 0.95f);

        pipe.pinnaManifoldInterpolator().process(g_ats.pdOutL, g_ats.pdOutR, nSamples);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis4_PinnaManifold,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.18f, 0.95f);

        pipe.transharmonicSynth().process(g_ats.pdOutL, g_ats.pdOutR, nSamples);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis2_Transharmonic,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.18f, 0.95f);

        pipe.shmMsoArbitrator().process(g_ats.pdOutL, g_ats.pdOutR, nSamples, srNow);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis5_MsoFarrow,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.15f, 0.95f);

        pipe.warpedLatticeInverter().process(g_ats.pdOutL, g_ats.pdOutR, nSamples);
        s_routeAGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis1_WarpedLattice,
            g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.18f, 0.95f);

        auto& realityOrch = ivanna::reality::AcousticRealityOrchestrator::instance();
        if (realityOrch.isEnabled()) {
            const auto rSnap = realityOrch.stateBus().readLatestSnapshot();
            if (rSnap.sequence > 0) {
                realityOrch.microExtractor().applyMicroIntelligibilityPass(
                    g_ats.pdOutL, g_ats.pdOutR, nSamples, rSnap.microMap);
                s_routeAGuard.enforceStageEnergyCeiling(
                    ivanna::supreme::AcousticModuleId::RealityReconstruction,
                    g_ats.pdOutL, g_ats.pdOutR, nSamples, 1.15f, 0.95f);
            }
        }
        // Acotar energía acumulada pre-limiter al rango lineal seguro (<= 0.92f)
        // para que SafetyLimiter nunca entre en compresión de pared ni dispare
        // falsos positivos de clipping en IvannaAgentCore / SelfHealingAgent.
        s_routeAGuard.processBlock(g_ats.pdOutL, g_ats.pdOutR, nSamples, 0.92f);
    }

    // ── SafetyLimiter + DC-block: ÚLTIMAS etapas reales de la Ruta A ────────
    // Corren DESPUÉS de M/S adaptativo, RIR, Volterra, Coclear y los 5 Ejes
    // Supremos para impedir cualquier pico > 0 dBFS o DC residual hacia AudioTrack.
    // v2.5.41: PUNCH justo antes del limitador (el limitador ve el resultado; el motor no sube el ataque si el pico
    // ya está en el techo, así que no hay «bombeo»). Latencia +1.5 ms solo mientras esté activo.
    g_punch.setSampleRate(static_cast<float>(g_params.sampleRate));
    g_punch.setImpactBias(ivanna::fourd::arcImpact());   // el drop del arco narrativo 4D realza el golpe (guardas intactas)
    g_temporal_arc_tension.store(ivanna::fourd::arcTension(), std::memory_order_relaxed);   // y modula el espacio: se abre antes, se recoge en el impacto
    g_temporal_arc_impact.store(ivanna::fourd::arcImpact(), std::memory_order_relaxed);
    routeAApplyNpe(g_ats.pdOutL, g_ats.pdOutR, n);   // paridad con Ruta B (NPE neuromorfico)
    g_punch.process(g_ats.pdOutL, g_ats.pdOutR, n);
    // Cancelación de diafonía de la Cadena 4D (solo altavoces/AUX; identidad si está apagada):
    // va ANTES del limitador porque la cancelación puede subir picos y el limitador es el techo.
    ivanna::fourd::applyOutputPlanar(g_ats.pdOutL, g_ats.pdOutR, n);
    g_safety_limiter.process(g_ats.pdOutL, g_ats.pdOutR, n);
    for (int i = 0; i < n; ++i) {
        g_ats.pdOutL[i] = g_dcBlockL.process(g_ats.pdOutL[i]);
        g_ats.pdOutR[i] = g_dcBlockR.process(g_ats.pdOutR[i]);
    }
    // Dosímetro sobre la señal FINAL (lo que llega al oído): la cadena DSP puede subir el nivel
    // respecto a la entrada, así que medir la entrada seca subestimaría la exposición real.
    ivanna::fourd::meterOutputPlanar(g_ats.pdOutL, g_ats.pdOutR, n);
    // v2.5.15: lazo cerrado — se mide el RESULTADO final (original vs salida) ANTES de la rampa de
    // arranque, para que ésta no sesgue la medición; luego se aplica el arranque en frío Hermite C1
    // (identidad una vez calentado; se rearma tras >= 250 ms de silencio).
    unityEngine.observeOutcome(g_ats.dryL, g_ats.dryR, g_ats.pdOutL, g_ats.pdOutR, static_cast<size_t>(n));
    unityEngine.applyColdStart(g_ats.pdOutL, g_ats.pdOutR, static_cast<size_t>(n));

    if (g_lab_auto_enabled.load(std::memory_order_relaxed)) {
        static thread_local int s_labFeedCounter = 0;
        if ((++s_labFeedCounter % 100) == 0) {
            static thread_local float labInter[2 * 2048];
            for (int i = 0; i < n; ++i) {
                float l = g_ats.pdOutL[i];
                float r = g_ats.pdOutR[i];
                if (!std::isfinite(l)) l = 0.f;
                if (!std::isfinite(r)) r = 0.f;
                labInter[2 * i]     = l;
                labInter[2 * i + 1] = r;
            }
            g_lab.setSampleRate(g_params.sampleRate);
            g_lab.feed(labInter, n);
            g_lab_auto_frame_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Re-intercalar el resultado estéreo real de vuelta en `data` — sin downmix.
    for (int i = 0; i < n; ++i) {
        float l = g_ats.pdOutL[i];
        float r = g_ats.pdOutR[i];
        if (!std::isfinite(l)) { l = 0.f; g_ats.nanRecoveries++; }
        if (!std::isfinite(r)) { r = 0.f; g_ats.nanRecoveries++; }
        data[2 * i]     = l;
        data[2 * i + 1] = r;
    }
    env->ReleaseFloatArrayElements(buf, data, 0);
    // Cierre del gobernador de shed: alimenta la deuda/EMA con el coste
    // REAL de la cadena (incluye preemption del hilo: si el planificador
    // nos quito el core a mitad de cadena, eso tambien es lateness).
    g_dspShedGovernor.observeRun(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - dspT0).count(),
        periodUs);
    logDspShedStateThrottled();
}
// Senal REAL de que omega_effect.so procesa audio en audioserver (Ruta B).
// -1 = no disponible (sin archivo / DAC o SELinux lo bloquea): el llamador debe CONSERVAR su
//      comportamiento previo. 0 = ningun efecto habilitado. 1 = habilitado sin bloques recientes
//      (pausa). 2 = procesando (bloque dentro de idleMs). Ver include/omega_effect_beacon.h.
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeEffectBeaconState(JNIEnv*, jobject, jint idleMs) {
    const uint64_t win = idleMs > 0 ? static_cast<uint64_t>(idleMs) : 2000ull;
    return static_cast<jint>(ivanna::readEffectBeacon(win));
}

// v2.5.386 — Contador de frames procesados por el efecto (beacon bytes 8-15).
// -1 = beacon no disponible (sin archivo / DAC o SELinux lo bloquea): el
// llamador debe CONSERVAR su comportamiento previo. >= 0 = frames acumulados
// desde que el efecto se creó. El watchdog de Ruta B lo usa para detectar el
// efecto HUÉRFANO (creado y habilitado pero con 0 frames mientras la música
// suena: el acople por sesión falló y hay que recrearlo en la sesión real).
JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeEffectBeaconFrames(JNIEnv*, jobject) {
    uint64_t frames = 0;
    const ivanna::EffectBeaconState st =
        ivanna::readEffectBeacon(2000ull, ivanna::OMEGA_EFFECT_BEACON_PATH, &frames, nullptr);
    if (st == ivanna::EffectBeaconState::Unavailable) return -1;
    return static_cast<jlong>(frames);
}

// FUSIÓN MAGISTRAL (v2.5.401, ABI v12) — Estado de la fusión para la APK.
// Lee /data/local/tmp/ivanna_fusion_heartbeat (lo escribe el cerebro v2;
// /data/local/tmp sí lo lee una app normal, /data/adb NO) y lo vuelca en
// `out` (9 floats): [fusion_active, eta, psi_gamma, psi_active, verdict,
// ratio, drift_ppm, clock, ts_ms]. Retorna 1 si el heartbeat es válido y
// fresco (<= 5 s; el cerebro escribe ~20 Hz), 0 si no hay fusión / está
// obsoleto. La app (AudioBackendSelector) lo lee cada 10 s: si
// fusion_active=1, la Ruta A cede (el HAL ya procesa TODO el audio).
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeFusionState(JNIEnv* env, jobject, jfloatArray out) {
    if (out == nullptr) return 0;
    if (env->GetArrayLength(out) < 9) return 0;
    std::FILE* f = std::fopen("/data/local/tmp/ivanna_fusion_heartbeat", "r");
    if (!f) return 0;
    double fusionActive = 0, eta = 0, psiGamma = 1, psiActive = 0, verdict = 1;
    double ratio = 1, driftPpm = 0, clock = 2, tsMs = 0;
    int alive = 0;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        char key[64]; double v = 0;
        if (std::sscanf(line, "%63[^=]=%lf", key, &v) != 2) continue;
        if (std::strcmp(key, "alive") == 0) alive = (v != 0.0);
        else if (std::strcmp(key, "fusion_active") == 0) fusionActive = v;
        else if (std::strcmp(key, "eta") == 0) eta = v;
        else if (std::strcmp(key, "psi_gamma") == 0) psiGamma = v;
        else if (std::strcmp(key, "psi_active") == 0) psiActive = v;
        else if (std::strcmp(key, "verdict") == 0) verdict = v;
        else if (std::strcmp(key, "ratio") == 0) ratio = v;
        else if (std::strcmp(key, "drift_ppm") == 0) driftPpm = v;
        else if (std::strcmp(key, "clock") == 0) clock = v;
        else if (std::strcmp(key, "ts_ms") == 0) tsMs = v;
    }
    std::fclose(f);
    if (!alive) return 0;
    // Frescura: el cerebro escribe ~20 Hz (mismo reloj monotónico); si el
    // sello tiene más de 5 s, el cerebro murió y la fusión ya no es de fiar.
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    if (tsMs <= 0 || nowMs < static_cast<uint64_t>(tsMs) ||
        (nowMs - static_cast<uint64_t>(tsMs)) > 5000ULL) return 0;
    const float vals[9] = {static_cast<float>(fusionActive), static_cast<float>(eta),
                           static_cast<float>(psiGamma), static_cast<float>(psiActive),
                           static_cast<float>(verdict), static_cast<float>(ratio),
                           static_cast<float>(driftPpm), static_cast<float>(clock),
                           static_cast<float>(tsMs)};
    env->SetFloatArrayRegion(out, 0, 9, vals);
    return 1;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReset(JNIEnv*, jobject) {
    g_dspShedGovernor.reset();
    g_pd.stop_evo_thread();
    g_eq.reset(); g_comp.reset(); g_exciter.reset();
    g_widener.reset(); g_gain.reset();
    g_safety_limiter.reset(); g_pd.reset();
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().reset();
    LOGI("OPE reset");
}
// ═══════════════════════════════════════════════════════════════════════════════
// IvannaNativeLib (com.ivanna.omega.core.IvannaNativeLib) — stereo block API
// ═══════════════════════════════════════════════════════════════════════════════
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitDSP(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 384000) return JNI_FALSE;  // paridad con nativeInit (9f99d4e6): nativas directas hasta 384k
    if (g_initialized.load(std::memory_order_acquire) && g_params.sampleRate == static_cast<uint32_t>(sr)) {
        (void)ensureAdaptiveEngineStarted();  // idempotente; respeta OFF/manual
        return JNI_TRUE;
    }
    std::lock_guard<std::mutex> lock(g_dspProcessMutex);
    if (g_initialized.load(std::memory_order_acquire) && g_params.sampleRate == static_cast<uint32_t>(sr)) {
        (void)ensureAdaptiveEngineStarted();
        return JNI_TRUE;
    }
    g_params.sampleRate = (uint32_t)sr;
    if (!g_routeAWfsEngine.prepare(static_cast<float>(sr), 2048)) {
        LOGE("Ruta A WFS: inicialización fallida; salida permanece en bypass");
    }
    routeAApplyDefaultXtcGeometry();
    g_eq.setParams(g_params); g_comp.setParams(g_params);
    g_exciter.setParams(g_params); g_widener.setParams(g_params);
    // Compensación de headroom EQ en init (misma lógica que nativeSetEQParams).
    { const float mDb = g_params.master;
      const float c   = g_eq.getOutputCompensationDb();
      g_params.master = std::clamp(mDb - c, -60.0f, 6.0f);
      g_gain.setParams(g_params);
      g_params.master = mDb; }
    g_pd.init((uint32_t)sr);
    g_pd.start_evo_thread();
    // FIX CRITICO (clipping/bombeo, 2026-08-27): mismo fix que nativeInit —
    // el limiter necesita SR real (ataque 1.5ms/release 50ms correctos) y
    // setParams() para el soft-knee (threshold -4 dBFS). Sin esto, esta ruta
    // limitaba con pared de ladrillo y constantes 8x cortas a 384 kHz
    // (ataque 0.19ms = distorsion armonica; release 6.25ms = bombeo).
    g_safety_limiter.setParams(0.891251f, 0.98855f);  // umbral -1 dBFS (antes -4: limitaba siempre)
    g_safety_limiter.setSampleRate((float)sr);
    g_crossfeed.setSampleRate((float)sr);
    g_orbeRouteA.prepare((float)sr);
    g_holosRouteA.prepare((float)sr);
    g_omniRouteA.prepare((float)sr);
    g_hyperRouteA.prepare((float)sr);
    g_neuroLoopRouteA.prepare((float)sr, 128);
    g_neuroBandEqRouteA.prepare((float)sr);
    // Eje Supremo: pre-compute biquad BPF + Heun coefficients for the cochlear engine.
    // Called here (UI thread, non-RT) — process() will never allocate.
    g_cochlearEngine.prepare(static_cast<float>(sr), 512);
    g_cochlearEngine.setIntensity(g_cochlearIntensity.load(std::memory_order_relaxed));
    if (g_rirConvolver.load(std::memory_order_acquire) == nullptr) {
        Ivanna::RirDataset* ds = new Ivanna::RirDataset();
        if (!ds->load("/data/adb/ivanna_omega/rir")) {
            delete ds; ds = nullptr;
            LOGI("Ruta A: sin dataset RIR en /data/adb/ivanna_omega/rir — usando Golden Master Studio BRIR cuando se active RIR");
        }
        g_rirDataset.store(ds, std::memory_order_release);
        auto* conv = new Ivanna::RirConvolver();
        conv->applySofaCoupling(ivanna::master::kMasterSafGoldenQ);
        const float initRt60 = g_localRoomRt60.load(std::memory_order_acquire);
        const float initWet  = g_localRoomWet.load(std::memory_order_acquire);
        conv->synthesizeMasterStudioBrir(initRt60 >= 0.01f ? initRt60 : 0.34f, sr);
        if (initRt60 >= 0.01f && initWet > 0.001f) {
            conv->setWetDry(initWet);
            if (ds && ds->isLoaded() && ds->roomCount() > 0) {
                const int32_t reqIdx = g_localRoomIdx.load(std::memory_order_acquire);
                const int32_t targetIdx = (reqIdx >= 0 && static_cast<size_t>(reqIdx) < ds->roomCount())
                    ? reqIdx
                    : static_cast<int32_t>(ds->findNearestSmart(initRt60));
                g_rirPendingIdx.store(targetIdx, std::memory_order_release);
            }
        } else {
            conv->setWetDry(0.0f);
        }
        g_rirConvolver.store(conv, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> uiLock(g_uiMutex);
        g_params_ui = g_params;
    }
    // FIX RT: arrancar el worker de carga de IR (hilo de control, joinable).
    // Idempotente — nativeInitDSP puede re-llamarse por cambio de SR.
    if (!g_rirWorkerRunning.exchange(true, std::memory_order_acq_rel)) {
        g_rirWorkerThread = std::thread(rirWorkerLoop);
        // NO detach — se une en JNI_OnUnload antes de los destructores.
    }
    {
        ivanna::spatial::IvannaAudioPipeline::setActiveInstance(&g_liveAudioPipeline);
        auto& pipe = ivanna::spatial::IvannaAudioPipeline::getActiveInstance();
        pipe.prepare(static_cast<float>(sr), 512);
        pipe.pinnaManifoldInterpolator().calibrateFromLatents(
            ivanna::master::kMasterSafGoldenQNorm[2],
            ivanna::master::kMasterSafGoldenQNorm[3],
            ivanna::master::kMasterSafGoldenQNorm[0],
            (float)sr);
    }
    // Cadena 4D: se prepara AQUÍ con el sample rate REAL del DSP (idempotente). Antes solo la
    // inicializaba el panel de la UI con 48000/480 fijos: sin abrir el panel, feed/apply eran
    // no-ops y con un dispositivo a 44.1 kHz el conductor temporizaba con un bloque erróneo.
    ivanna::fourd::ensureInit(static_cast<float>(sr), ivanna::fourd::kAnalysisChunk);
    g_initialized.store(true, std::memory_order_release);
    // Misma regla que nativeInit: el ADE arranca cuando el grafo ya está preparado. Esta es la
    // ruta que usa MainActivity; sin esto una preferencia ON fijada antes del DSP esperaba a
    // nativeCreateAdaptiveEngine/watchdog. Respeta OFF/manual y es idempotente.
    if (ensureAdaptiveEngineStarted()) {
        LOGI("AdaptiveDecisionEngine running (nativeInitDSP, control thread @50ms)");
    } else {
        LOGI("AdaptiveDecisionEngine disabled by preference or start unavailable (nativeInitDSP)");
    }
    LOGI("IvannaNativeLib DSP @ %d Hz (EvolutionaryKernel online)", sr);
    return JNI_TRUE;
}

// ── Sensor acústico real → corrección de sala en vivo (Ruta A, v2.5.369) ───
// Bucle cerrado por micrófono, en el hilo de audio, SIN bloquear jamás:
//   1. Lee el último SensorFrame (lock-free, seqlock).
//   2. Si el bucle está habilitado y el frame es válido y fresco (nuevo
//      frame_index en el último segundo): traduce error_db ERB →
//      corrección RoomProbe (10 bandas) y la pone como objetivo del
//      RoomCorrectionEq (que desliza a 0.25 dB/bloque, sin clics).
//      Si el frame es inválido o rancio: objetivo 0 (la corrección se
//      desvanece sola, fail-closed).
//   3. Aplica la corrección (bit-exacta si el objetivo es 0).
//   4. Empuja la salida mono (post-corrección = lo que sale al altavoz)
//      al anillo SPSC de referencia del sensor (lock-free, sin mutex).
// La corrección es LENTA (frame de ~85 ms + suavizado 250 ms + slew del
// EQ): es una medida de sala, no una corrección muestra a muestra. Por
// eso el impacto es tonal (boom/valle de sala), no un cambio instantáneo.
// Aproximación honesta: el micrófono mide el punto donde está el
// teléfono, no los oídos; la corrección es la del balance tonal en ese
// punto (ver docs/REAL_ACOUSTIC_SENSOR.md).
// NOTA: aquí NUNCA se llama a g_acousticSensorA.prepare() (toma el mutex
// del sensor y bloquearía el audio). El sensor se prepara en
// nativeSensorInit (hilo de Kotlin, al arrancar la captura) y su
// constructor ya lo deja en 48 kHz.
// Hiper-Operador Φ en Ruta A (v2.5.369). Se llama tras HOLOS/ORBE en
// nativeProcess y nativeProcessBlock, ANTES de la RIR y del limiter.
// Alimenta al operador con el SensorFrame vivo del micrófono (el sensor
// corre en su propio hilo; latestFrame() es lock-free) y aplica amount /
// preset / XTC desde los atomics. amount=0 → retorno inmediato, cero
// coste (el operador es identidad bit-exacta apagado). Registra en
// g_hyperMeasuredA si el operador está en modo MEDIDO: en ese caso
// sensorRoomCorrectionRouteA NO aplica su propia corrección (la Γ del
// operador YA es la corrección; dos EQs sobre el mismo error duplicarían
// la corrección). En modo APROXIMADO la guarda no se activa: el EQ de
// sala del sensor (apagado por defecto) sigue disponible.
static void hyperProcessRouteA(float* oL, float* oR, int n, float sr) noexcept {
    const float amount = g_hyper_amount.load(std::memory_order_relaxed);
    if (amount <= 0.0f || oL == nullptr || oR == nullptr || n <= 0) {
        g_hyperMeasuredA.store(false, std::memory_order_relaxed);
        return;
    }
    static float preparedSr = 0.0f;
    const float s = (std::isfinite(sr) && sr >= 8000.0f) ? sr : 48000.0f;
    if (preparedSr != s) {
        g_hyperRouteA.prepare(s);
        preparedSr = s;
    }
    const ivanna::spatial::SensorFrame fr = g_acousticSensorA.latestFrame();
    g_hyperRouteA.setSensorFrame(fr);
    g_hyperRouteA.setAmount(amount);
    const int preset = g_hyper_preset.load(std::memory_order_relaxed);
    g_hyperRouteA.setRoutePreset(
        static_cast<ivanna::spatial::HyperRoutePreset>(
            std::clamp(preset, 0, 2)));
    g_hyperRouteA.setXtcEnabled(g_hyper_xtc.load(std::memory_order_relaxed));
    g_hyperRouteA.processBlock(oL, oR, n);
    g_hyperMeasuredA.store(
        g_hyperRouteA.status().mode == ivanna::spatial::HyperMode::Measured,
        std::memory_order_relaxed);
}

static void sensorRoomCorrectionRouteA(float* oL, float* oR, int n, float sr) noexcept {
    static float preparedSr = 0.0f;
    const float s = (std::isfinite(sr) && sr >= 8000.0f) ? sr : 48000.0f;
    if (preparedSr != s) {
        g_sensorRoomEqA.prepare(s);
        preparedSr = s;
    }
    const ivanna::spatial::SensorFrame fr = g_acousticSensorA.latestFrame();
    const bool loopOn = g_sensorRoomCorrEnabledA.load(std::memory_order_relaxed);
    const float amount = g_sensorRoomCorrAmountA.load(std::memory_order_relaxed);
    static std::chrono::steady_clock::time_point lastNewFrame =
        std::chrono::steady_clock::now();
    static uint64_t lastSeenIndex = 0;
    if (fr.valid && fr.frame_index != lastSeenIndex) {
        lastSeenIndex = fr.frame_index;
        lastNewFrame = std::chrono::steady_clock::now();
    }
    const bool fresh = fr.valid &&
        (std::chrono::steady_clock::now() - lastNewFrame) < std::chrono::seconds(1);
    float corrDb[10] = {};
    // Guarda anti-doble-corrección (v2.5.369): si el operador Φ está en
    // modo MEDIDO, su Γ ya corrige este mismo error_db — el EQ de sala
    // no aplica nada (corrDb queda en 0). Sin Φ medido, se comporta igual
    // que antes.
    const bool hyperMeasured = g_hyperMeasuredA.load(std::memory_order_relaxed);
    if (loopOn && fresh && amount > 0.0f && !hyperMeasured) {
        g_acousticSensorA.mapErrorToRoomCorrection(fr.error_db, amount, corrDb);
    }
    g_sensorRoomEqA.setTarget(corrDb);
    if (oL != nullptr && oR != nullptr && n > 0) {
        g_sensorRoomEqA.process(oL, oR, n);
        // Referencia: mezcla mono post-corrección (lo que sale al altavoz).
        float mono[2048];
        const int m = std::min(n, 2048);
        for (int i = 0; i < m; ++i) mono[i] = 0.5f * (oL[i] + oR[i]);
        g_acousticSensorA.pushReference(mono, (std::size_t)m);
    }
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeProcessBlock(
    JNIEnv* env, jobject,
    jfloatArray inL, jfloatArray inR,
    jfloatArray outL, jfloatArray outR,
    jint frames) {
    if (frames <= 0) return;
    if (!inL || !inR || !outL || !outR) return;
    // Stack buffers — zero allocations
    float lBuf[2048], rBuf[2048], oL[2048], oR[2048];
    // n acotado por la longitud REAL de los cuatro arrays: los memcpy de salida
    // escriben n floats y un array de salida corto desbordaba el heap de la JVM.
    const int n = std::min({(int)frames, 2048,
                            (int)env->GetArrayLength(inL), (int)env->GetArrayLength(inR),
                            (int)env->GetArrayLength(outL), (int)env->GetArrayLength(outR)});
    if (n <= 0) return;
    if (!copyJFloat(env, inL, lBuf, n)) return;
    if (!copyJFloat(env, inR, rBuf, n)) return;
    NonBlockingDspProcessGuard dspGuard;
    if (!dspGuard.acquired || !g_initialized.load(std::memory_order_acquire)) {
        jfloat* pL = outL ? env->GetFloatArrayElements(outL, nullptr) : nullptr;
        jfloat* pR = outR ? env->GetFloatArrayElements(outR, nullptr) : nullptr;
        if (pL) { memcpy(pL, lBuf, n * sizeof(float)); env->ReleaseFloatArrayElements(outL, pL, 0); }
        if (pR) { memcpy(pR, rBuf, n * sizeof(float)); env->ReleaseFloatArrayElements(outR, pR, 0); }
        return;
    }
    drainPendingDspParamsLocked();
    // Cadena 4D (v2.5.48): alimentación con la entrada SECA (lBuf/rBuf aún sin DSP).
    // Apagada por defecto = retorno inmediato, cero bits alterados.
    ivanna::fourd::feedPlanar(lBuf, rBuf, n);
    ivanna::fourd::publishAnticipatedScene();
    // DSP chain
    // Adaptive decisions: mismos atomics que actualiza nativeProcess cuando
    // consumeIfNewer() trae un AdaptiveState nuevo. thread_local smooth
    // independiente (nativeProcessBlock puede correr en thread distinto o
    // el mismo; en ambos casos converge a los mismos valores del ADE).
    // Sin malloc, sin mutex — solo atomic loads + EMA, idéntico patrón al
    // bloque de P0 en nativeProcess.
    // estado en g_ats (ver struct AudioThreadState)
    const float adaptiveStrength = adaptive_ui_strength();
    const bool adaptiveActive = adaptiveStrength > 0.0f;
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
    if (!adaptiveActive) {
        // OFF/manual: clear this consumer's independent ADE smoothers.
        // Compressor user settings survive; setRuntimeAmount below smoothly
        // returns its runtime overlay to neutral without hard-resetting a
        // shared actuator that may still be serving the Perceptual Guard.
        g_ats.blkTgSmooth = 1.0f;
        g_ats.blkCaSmooth = 0.0f;
        g_ats.blkErSmooth = 0.0f;
        g_gain.setRuntimeGain(1.0f);
        g_exciter.setRuntimeReduction(0.0f);
    } else {
        g_ats.blkTgSmooth += 0.05f * (blkTargetGain - g_ats.blkTgSmooth);
        g_ats.blkCaSmooth += 0.05f * (blkCompAmount - g_ats.blkCaSmooth);
        g_ats.blkErSmooth += 0.05f * (blkExcReduction - g_ats.blkErSmooth);
    }
    g_gain.processInput(lBuf, rBuf, n);
    // Guard de pico previo al EQ (campo separado blkPeakGuardScale: este path puede correr en un
    // hilo distinto, mismo patrón que blkTgSmooth vs targetGainSmooth). Lógica en BlockPeakGuard.h.
    ivanna::blockPeakGuard(lBuf, rBuf, n, g_ats.blkPeakGuardScale);
    g_eq.process(lBuf, rBuf, n);
    routeAApplyEvoEq(lBuf, rBuf, n);   // paridad con Ruta B (EQ evolutivo 512 bandas)
    g_gain.setRuntimeGain(g_ats.blkTgSmooth);
    g_comp.setRuntimeAmount(g_ats.blkCaSmooth);
    g_comp.process(lBuf, rBuf, n);
    g_exciter.setRuntimeReduction(g_ats.blkErSmooth);

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
        const float sp_angle = std::clamp((g_control_frame.evo_genome_spatial[0].load(std::memory_order_relaxed) - 0.5f) * 60.f, -30.f, 30.f);
        const float sp_width = std::clamp(g_control_frame.evo_genome_spatial[1].load(std::memory_order_relaxed) * 1.5f, 0.f, 1.5f);
        g_pd.set_nho_alpha(nho_a);
        g_pd.set_nho_beta(nho_b);
        g_pd.set_nho_harmonic(nho_h);
        g_pd.set_spatial_angle(sp_angle);
        g_pd.set_spatial_width(sp_width);
    }
    // PDEngine (NHO + Spatial on modes 1/2)
    g_pd.process_block(lBuf, rBuf, oL, oR, n);
    // Misma etapa WFS que DSPBridge.nativeProcess: evita divergencia entre APIs JNI.
    processRouteAWfs(oL, oR, n);

    // ── ORBE + HOLOS en serie (Ruta A/C, v2.5.363/364; v2.5.369: AMBOS activos
    // por directiva explícita). Antes era if/else (HOLOS ocupaba el lugar de
    // ORBE); con ambos defaults > 0, ORBE quedaba muerto. Ahora ORBE corre
    // primero (render esférico de campo cercano) y HOLOS después (operador
    // unificado, K=0 retuneado, limpio). Comparte g_orbeRouteA/g_holosRouteA
    // con nativeProcess (nunca corren a la vez: g_dspProcessMutex). Cada uno
    // es bypass bit-exacto de coste cero si su amount=0.
    // Bucle cerrado neuro-acústico (v2.5.365): 1x por bloque, ANTES de
    // ORBE/HOLOS — NeuroBandEq + deltas acotados sobre hp/op.
    const auto neuroAdjA = neuroLoopRouteAUpdate(
        oL, oR, n, static_cast<float>(std::max(g_params.sampleRate, 8000u)));
    {
        // ORBE primero.
        const float orbeAmount = g_orbe_amount.load(std::memory_order_relaxed);
        if (orbeAmount > 0.0f) {
            ivanna::OrbeParams op;
            op.amount = orbeAmount;
            op.distanceM = g_orbe_distance_m.load(std::memory_order_relaxed);
            op.spread = g_orbe_spread.load(std::memory_order_relaxed);
            op.proximityDb = g_orbe_proximity_db.load(std::memory_order_relaxed);
            op.diffuse = g_orbe_diffuse.load(std::memory_order_relaxed);
            op.headTracking = g_orbe_head_tracking.load(std::memory_order_relaxed);
            op.headRadiusM = g_orbe_head_radius_m.load(std::memory_order_relaxed);
            op.bandCount = 24;
            {   // Bucle cerrado: deltas locales sobre la base del usuario.
                const auto tgt = neuroLoopTargetsOrbe(neuroAdjA);
                op.distanceM = tgt.orbeDistanceM;
                op.proximityDb = tgt.orbeProximityDb;
                op.diffuse = tgt.holosDiffuse;
            }
            g_orbeRouteA.setThermalShed(g_spatialShedA.load(std::memory_order_relaxed));
            g_orbeRouteA.setParams(op);
            g_orbeRouteA.process(oL, oR, n);
        }
        // HOLOS después.
        const float holosAmount = g_holos_amount.load(std::memory_order_relaxed);
        if (holosAmount > 0.0f) {
            ivanna::HolosParams hp;
            hp.amount = holosAmount;
            hp.distanceM = g_holos_distance_m.load(std::memory_order_relaxed);
            hp.spread = g_holos_spread.load(std::memory_order_relaxed);
            hp.proximityDb = g_holos_proximity_db.load(std::memory_order_relaxed);
            hp.diffuse = g_holos_diffuse.load(std::memory_order_relaxed);
            hp.harmonics = std::clamp(g_holos_harmonics.load(std::memory_order_relaxed), 0.0f, ivanna::kHolosHarmonicsMaxValidated);  // guarda v2.5.411: el valor guardado (p. ej. 0.298) nunca llega al DSP sin validar
            hp.warmth = g_holos_warmth.load(std::memory_order_relaxed);
            hp.dynamics = g_holos_dynamics.load(std::memory_order_relaxed);
            hp.width = g_holos_width.load(std::memory_order_relaxed);
            hp.gamma = g_holos_gamma.load(std::memory_order_relaxed);
            hp.roomRt60 = g_holos_room_rt60.load(std::memory_order_relaxed);
            hp.headTracking = g_holos_head_tracking.load(std::memory_order_relaxed);
            hp.eqLow = g_holos_eq_low.load(std::memory_order_relaxed);
            hp.eqMid = g_holos_eq_mid.load(std::memory_order_relaxed);
            hp.eqHigh = g_holos_eq_high.load(std::memory_order_relaxed);
            hp.eqPresence = g_holos_eq_presence.load(std::memory_order_relaxed);
            hp.eqTiltDb = g_holos_eq_tilt_db.load(std::memory_order_relaxed);
            hp.eqFreq = g_holos_eq_freq.load(std::memory_order_relaxed);
            hp.bandCount = (g_holos_band_count.load(std::memory_order_relaxed) == 12) ? 12 : 24;
            {   // Bucle cerrado: deltas locales sobre la base del usuario.
                const auto tgt = neuroLoopTargetsHolos(neuroAdjA);
                hp.gamma = tgt.holosGamma;
                hp.width = tgt.holosWidth;
                hp.diffuse = tgt.holosDiffuse;
                hp.distanceM = tgt.orbeDistanceM;
                hp.proximityDb = tgt.orbeProximityDb;
            }
            g_holosRouteA.setThermalTier(static_cast<ivanna::HolosThermalTier>(
                std::clamp(g_holosTierA.load(std::memory_order_relaxed), 0, 3)));
            g_holosRouteA.setParams(hp);
            g_holosRouteA.process(oL, oR, n);
        }
    }

    // IVANNA (v2.5.375): tras HOLOS, antes de Φ (ver omniProcessRouteA).
    omniProcessRouteA(oL, oR, n);

    // Hiper-Operador Φ (v2.5.369): tras HOLOS/ORBE, antes de la RIR y del
    // limiter. Es la etapa que el usuario ESCUCHA en Ruta A cuando
    // hyper_amount > 0. amount=0 = no-op.
    hyperProcessRouteA(oL, oR, n,
                       static_cast<float>(std::max(g_params.sampleRate, 8000u)));

    // ── RIR en nativeProcessBlock (PlaybackCaptureService) ─────────────────
    if (Ivanna::RirConvolver* s_rirConvolver = g_rirConvolver.load(std::memory_order_acquire)) {
        if (s_rirConvolver->isLoaded()) {
            s_rirConvolver->process(oL, oR, n);
        }
    }

    // ── Supremacía Acústica: Volterra H2 (Anti-Lossy Reconstruction) ──
    if (g_volterra_enabled.load(std::memory_order_relaxed)) {
        float inter[4096];
        const int samples = std::min(n, 2048);
        for (int i = 0; i < samples; ++i) {
            inter[2 * i]     = oL[i];
            inter[2 * i + 1] = oR[i];
        }
        g_volterra_engine.processInterleaved(inter, inter, (uint32_t)samples, 2);
        for (int i = 0; i < samples; ++i) {
            oL[i] = inter[2 * i];
            oR[i] = inter[2 * i + 1];
        }
    }

    // ── Eje Supremo: Inversión Biomecánica Coclear Activa (Cochlear-PINN) ────
    // Post-Volterra, pre-limiter: la señal está en su nivel final de mezcla.
    // 0.00 ms de latencia añadida. Rampa suave interna al activar/desactivar.
    g_cochlearEngine.setEnabled(g_cochlearEnabled.load(std::memory_order_relaxed));
    g_cochlearEngine.process(oL, oR, n);

    // ── Ejes 1–6 Espaciales + 5 Ejes de Supremacía Cuántico-Neuromórfica (Ruta A / Ruta C) ─────────
    {
        static ivanna::supreme::SupremeAcousticStabilityGuard s_blkGuard{};
        auto& pipe = ivanna::spatial::IvannaAudioPipeline::getActiveInstance();
        const float srNow = static_cast<float>(std::max(g_params.sampleRate, 8000u));
        const size_t nSamples = static_cast<size_t>(n);

        s_blkGuard.beginBlock(oL, oR, nSamples, false);
        const bool allowLiveSpatial =
            !g_upmixing_enabled.load(std::memory_order_relaxed) &&
            !routeAWfsAllowed();
        ivanna::ime::imeFeedPlanar(oL, oR, n, srNow);
        ivanna_pilstm_bridge_tick_block(oL, oR, n, srNow);
        pipe.processLiveSpatialAxes(
            oL, oR, nSamples, srNow, allowLiveSpatial, 0.16f,
            /*runHearingStage=*/true, /*runCochlearStage=*/false);
        s_blkGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::ObjectRenderer,
            oL, oR, nSamples, 1.20f, 0.95f);
        pipe.snnNmfHoaUpmixer().process(oL, oR, nSamples);
        s_blkGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis3_SnnNmfHoa,
            oL, oR, nSamples, 1.18f, 0.95f);
        pipe.pinnaManifoldInterpolator().process(oL, oR, nSamples);
        s_blkGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis4_PinnaManifold,
            oL, oR, nSamples, 1.18f, 0.95f);
        pipe.transharmonicSynth().process(oL, oR, nSamples);
        s_blkGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis2_Transharmonic,
            oL, oR, nSamples, 1.18f, 0.95f);
        pipe.shmMsoArbitrator().process(oL, oR, nSamples, srNow);
        s_blkGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis5_MsoFarrow,
            oL, oR, nSamples, 1.15f, 0.95f);
        pipe.warpedLatticeInverter().process(oL, oR, nSamples);
        s_blkGuard.enforceStageEnergyCeiling(
            ivanna::supreme::AcousticModuleId::SupremeAxis1_WarpedLattice,
            oL, oR, nSamples, 1.18f, 0.95f);
        s_blkGuard.processBlock(oL, oR, nSamples, 0.92f);
    }

    // SafetyLimiter DESPUÉS de PDEngine — único punto de limiting, sobre la
    // señal con nivel final. Sin él aquí, bloques que superen 0 dBFS tras
    // NHO/Spatial saldrían sin protección hacia el DAC.
    g_punch.setSampleRate(static_cast<float>(g_params.sampleRate));   // v2.5.41
    g_punch.setImpactBias(ivanna::fourd::arcImpact());
    g_temporal_arc_tension.store(ivanna::fourd::arcTension(), std::memory_order_relaxed);
    g_temporal_arc_impact.store(ivanna::fourd::arcImpact(), std::memory_order_relaxed);
    routeAApplyNpe(oL, oR, n);   // paridad con Ruta B (NPE neuromorfico)
    g_punch.process(oL, oR, n);
    // Cancelación de diafonía de la Cadena 4D antes del limitador (ver nativeProcess).
    ivanna::fourd::applyOutputPlanar(oL, oR, n);
    g_safety_limiter.process(oL, oR, n);
    // FIX (safety net — mismo motivo que en nativeProcess, ver ese
    // comentario extenso): NaN/Inf nunca se saneaba en esta ruta tampoco.
    // Campo separado (blkNanRecoveries) por el mismo motivo que el resto
    // del estado de este path está separado — puede correr en otro hilo.
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(oL[i])) { oL[i] = 0.f; g_ats.blkNanRecoveries++; }
        if (!std::isfinite(oR[i])) { oR[i] = 0.f; g_ats.blkNanRecoveries++; }
    }
    // ── Sensor acústico real → corrección de sala en vivo (Ruta A, v2.5.369) ──
    // Última etapa: la corrección medida por el micrófono (RoomCorrectionEq,
    // bit-exacta si el bucle está apagado) y la salida mono alimenta la
    // referencia del sensor (coherencia). Ver sensorRoomCorrectionRouteA().
    sensorRoomCorrectionRouteA(oL, oR, n, static_cast<float>(std::max(g_params.sampleRate, 8000u)));
    // Dosímetro sobre la señal final saneada (ver nativeProcess).
    ivanna::fourd::meterOutputPlanar(oL, oR, n);
    jfloat* pL = env->GetFloatArrayElements(outL, nullptr);
    jfloat* pR = env->GetFloatArrayElements(outR, nullptr);
    if (pL) { memcpy(pL, oL, n*sizeof(float)); env->ReleaseFloatArrayElements(outL, pL, 0); }
    if (pR) { memcpy(pR, oR, n*sizeof(float)); env->ReleaseFloatArrayElements(outR, pR, 0); }
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNpeLocal(
    JNIEnv*, jobject, jboolean enabled, jfloat harmonic, jfloat lateralInhib, jfloat ohcCompression, jfloat masterDb) {
    // Ruta A: mismos rangos que NpeStereoCore::setParams. Valores no finitos se ignoran (se conserva el anterior).
    if (std::isfinite(harmonic))       g_npeAHarm.store(std::clamp(harmonic, 0.0f, 2.0f), std::memory_order_relaxed);
    if (std::isfinite(lateralInhib))   g_npeALat.store(std::clamp(lateralInhib, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(ohcCompression)) g_npeAOhc.store(std::clamp(ohcCompression, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(masterDb))       g_npeAMasterDb.store(std::clamp(masterDb, -24.0f, 12.0f), std::memory_order_relaxed);
    g_npeAOn.store(enabled == JNI_TRUE, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEvolutionaryEqEnabled(JNIEnv*, jobject, jboolean enabled) {
    g_evoEqAEnabled.store(enabled == JNI_TRUE, std::memory_order_relaxed);
}
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsEvolutionaryEqActive(JNIEnv*, jobject) {
    // Read-back de estado (calibrado y habilitado); no es una medicion de la senal.
    return (g_evoEqA.load(std::memory_order_acquire) != nullptr && g_evoEqAEnabled.load(std::memory_order_relaxed))
               ? JNI_TRUE : JNI_FALSE;
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

// ═══════════════════════════════════════════════════════════════════════════
// FASE 2 (NAEL / ISO 226:2023) — puente JNI
//   nativeSetNaelEnabled(boolean) : activa/desactiva compensación.
//   nativeGetNaelCorrections()    : FloatArray[10], dB por banda ISO
//                                   (31.5/63/125/250/500/1k/2k/4k/8k/16k).
// ═══════════════════════════════════════════════════════════════════════════
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNaelEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_nael_enabled.store(on, std::memory_order_relaxed);
    if (!on) ivanna::iso226_reset();
    LOGI("NAEL (ISO 226:2023) %s", on ? "ENABLED" : "disabled");
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsIso226Enabled(
    JNIEnv*, jobject) {
    return g_nael_enabled.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetNaelCorrections(
    JNIEnv* env, jobject) {
    jfloatArray out = env->NewFloatArray(ivanna::NAEL_NUM_BANDS);
    if (!out) return nullptr;
    jfloat vals[ivanna::NAEL_NUM_BANDS];
    for (int b = 0; b < ivanna::NAEL_NUM_BANDS; ++b) {
        vals[b] = ivanna::iso226_get_smoothed(b);
    }
    env->SetFloatArrayRegion(out, 0, ivanna::NAEL_NUM_BANDS, vals);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// FASE 3 (IvannaLab auto-feed) — activa medición automática desde la UI.
// ═══════════════════════════════════════════════════════════════════════════
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetLabAutoEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_lab_auto_enabled.store(on, std::memory_order_relaxed);
    if (on) g_lab_auto_frame_count.store(0, std::memory_order_relaxed);
    LOGI("IvannaLab auto-feed %s", on ? "ENABLED" : "disabled");
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsLabAutoEnabled(
    JNIEnv*, jobject) {
    return g_lab_auto_enabled.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetLabAutoFrameCount(
    JNIEnv*, jobject) {
    return (jint)g_lab_auto_frame_count.load(std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetLoudnessTarget(
    JNIEnv*, jobject, jfloat targetLufs) {
    if (!std::isfinite(targetLufs)) return;
    const float clamped = std::clamp((float)targetLufs, -36.0f, -6.0f);
    g_loudness_target.store(clamped, std::memory_order_relaxed);
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.loudness_target = clamped;
        bus.publish(snap);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetLoudnessTrimEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    g_loudness_trim_enabled.store(enabled == JNI_TRUE, std::memory_order_relaxed);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAudioSpectrum(
    JNIEnv* env, jobject) {
    jfloatArray out = env->NewFloatArray(ivanna::BEB_BANDS);
    if (!out) return nullptr;
    jfloat vals[ivanna::BEB_BANDS];
    for (int b = 0; b < ivanna::BEB_BANDS; ++b) {
        vals[b] = 0.5f * (g_pd.cue_bank.envL[b] + g_pd.cue_bank.envR[b]);
    }
    env->SetFloatArrayRegion(out, 0, ivanna::BEB_BANDS, vals);
    return out;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetPerceptualCues(
    JNIEnv* env, jobject) {
    jfloatArray out = env->NewFloatArray(4);
    if (!out) return nullptr;
    const jfloat vals[4] = {
        g_pd.last_cues.L,
        g_pd.last_cues.T,
        g_pd.last_cues.S,
        g_pd.last_cues.R
    };
    env->SetFloatArrayRegion(out, 0, 4, vals);
    return out;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRunNeuralBenchmarks(
    JNIEnv* env, jobject) {
    jfloatArray out = env->NewFloatArray(4);
    if (!out) return nullptr;
    constexpr int kN = 256;
    float inL[kN] = {}, inR[kN] = {}, outL[kN] = {}, outR[kN] = {};
    inL[0] = 0.5f; inR[0] = -0.5f;
    auto t0 = std::chrono::steady_clock::now();
    ivanna::NHOEngine benchNho;
    benchNho.process_block(inL, inR, outL, outR, kN);
    auto t1 = std::chrono::steady_clock::now();
    ivanna::BiquadEnvelopeBank benchBeb;
    benchBeb.init(48000);
    (void)benchBeb.process_block(inL, inR, kN);
    auto t2 = std::chrono::steady_clock::now();
    ivanna::CueBasedSpatial benchSp;
    benchSp.process_block(inL, inR, outL, outR, kN, 48000.f);
    auto t3 = std::chrono::steady_clock::now();
    const float us0 = std::chrono::duration<float, std::micro>(t1 - t0).count();
    const float us1 = std::chrono::duration<float, std::micro>(t2 - t1).count();
    const float us2 = std::chrono::duration<float, std::micro>(t3 - t2).count();
    const jfloat vals[4] = { us0, us1, us2, us0 + us1 + us2 };
    env->SetFloatArrayRegion(out, 0, 4, vals);
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// JNI API: Volterra H2, FastRPC Hexagon cDSP & Active Transducer Inversion
// ═══════════════════════════════════════════════════════════════════════════

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetVolterraEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_volterra_enabled.store(on, std::memory_order_release);
    // v2.5.369 (Genezi): el interruptor gobierna TAMBIÉN el envelope del
    // motor. Sin esto, un kernel cargado (loadKernelPackage deja
    // m_enabled==false) nunca se activaría al encender Volterra, y un
    // kernel activo no fundearía al apagarlo. Espejo de Ruta B, donde
    // volterraTargetEnabled hace setEnabled() en ctx->volterraEngine.
    g_volterra_engine.setEnabled(on);
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        if (on) snap.flags |= ivanna::OMEGA_FLAG_VOLTERRA_ON;
        else    snap.flags &= ~ivanna::OMEGA_FLAG_VOLTERRA_ON;
        bus.publish(snap);   // no-op en la app (solo lector del bus): se conserva por si el bus es escribible
    }
    // v2.5.16: el camino que SÍ llega a la Ruta B (audioserver) es app -> daemon -> snapshot.
    ivanna::omegaSendUserFlag("volterra", on ? 1 : 0);
    LOGI("[IvannaNativeLib] Volterra H2 Anti-Lossy Reconstruction set to %d", on);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsVolterraEnabled(
    JNIEnv*, jobject) {
    return g_volterra_enabled.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

// v2.5.369 (Genezi MHC-PZ1D): carga un paquete IVK1 en el motor Volterra
// H2 de Ruta A. Hilo de CONTROL, nunca el de audio (loadKernelPackage
// puede llamar memalign/free). Contrato del motor:
//   1. setEnabled(false) -> el envelope fundea a silencio
//   2. loadKernelPackage(...) == Ok (política de producción por defecto:
//      fs 48000, K 64, device MOTO_G85_GENEZY, sum|h1| <= 1.000001)
//   3. en éxito se restaura setEnabled(g_volterra_enabled) -> el kernel
//      suena si Volterra está encendido (ON por defecto en v2.5.369; el
//      usuario puede apagarlo y persiste en ParameterStore).
// Ante CUALQUIER error el motor queda en bypass (setEnabled(false)) y los
// kernels previos quedan intactos. En éxito NO habilita el motor si
// Volterra estaba apagado. Cargar != habilitar.
// Devuelve el VolterraLoadStatus como int (0 = Ok). Ver
// neuromorphic/volterra_kernel_package.hpp.
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLoadVolterraKernelPackage(
    JNIEnv* env, jobject, jbyteArray pkg) {
    if (pkg == nullptr) {
        LOGE("[IvannaNativeLib] nativeLoadVolterraKernelPackage: pkg null");
        return static_cast<jint>(ivanna::dsp::VolterraLoadStatus::NullArgument);
    }
    const jsize len = env->GetArrayLength(pkg);
    if (len <= 0) {
        LOGE("[IvannaNativeLib] nativeLoadVolterraKernelPackage: pkg vacío");
        return static_cast<jint>(ivanna::dsp::VolterraLoadStatus::Truncated);
    }
    jbyte* raw = env->GetByteArrayElements(pkg, nullptr);
    if (raw == nullptr) {
        LOGE("[IvannaNativeLib] nativeLoadVolterraKernelPackage: no se pudo leer el array");
        return static_cast<jint>(ivanna::dsp::VolterraLoadStatus::NullArgument);
    }
    // Copia local: loadKernelPackage valida y aplica desde el buffer.
    std::vector<std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t*>(raw),
        reinterpret_cast<const std::uint8_t*>(raw) + len);
    env->ReleaseByteArrayElements(pkg, raw, JNI_ABORT);
    // Contrato del motor: deshabilitar antes de cargar (si está habilitado
    // devuelve EngineBusy sin tocar nada).
    g_volterra_engine.setEnabled(false);
    ivanna::dsp::VolterraLoadPolicy prod;  // producción (GENEZY01 / MOTO_G85_GENEZY)
    const ivanna::dsp::VolterraLoadStatus st =
        g_volterra_engine.loadKernelPackage(bytes.data(), bytes.size(), prod);
    if (st == ivanna::dsp::VolterraLoadStatus::Ok) {
        // Restaura el estado que el usuario tenía: el kernel solo suena si
        // Volterra estaba encendido. Si estaba apagado, queda cargado pero
        // en bypass hasta que el usuario lo encienda explícitamente.
        g_volterra_engine.setEnabled(g_volterra_enabled.load(std::memory_order_acquire));
        LOGI("[IvannaNativeLib] Volterra kernel package cargado (Genezi): "
             "hasEffectiveKernel=%d enabled=%d",
             g_volterra_engine.hasEffectiveKernel() ? 1 : 0,
             g_volterra_engine.isEnabled() ? 1 : 0);
    } else {
        LOGE("[IvannaNativeLib] Volterra kernel package RECHAZADO: %s (%d)",
             ivanna::dsp::volterraLoadStatusName(st), static_cast<int>(st));
    }
    return static_cast<jint>(st);
}

// v2.5.369 (Genezi): ¿tiene el motor de Ruta A un kernel efectivo cargado?
// true = hay paquete aplicado (no implica que esté sonando: depende de
// nativeIsVolterraEnabled()).
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeHasVolterraKernel(
    JNIEnv*, jobject) {
    return g_volterra_engine.hasEffectiveKernel() ? JNI_TRUE : JNI_FALSE;
}

// v2.5.369 (selector PZ1D / Genezi): reemplaza el paquete IVK1 del motor
// Volterra H2 de Ruta A EN RUNTIME, sin reiniciar ni cortar el audio.
// Es la vía que usa el selector de la UI; nativeLoadVolterraKernelPackage
// queda como primera carga (arranque). Contrato (motor,
// VolterraH2Symmetric::replaceKernelPackage):
//   1. la puerta de producción valida el paquete nuevo PRIMERO; si lo
//      rechaza, el motor no se toca (sigue sonando el kernel anterior).
//   2. setEnabled(false) -> el envelope fundea a silencio en ~18 ms; se
//      espera quiescencia con tope ~300 ms (silencio real, o ganancia
//      congelada ~50 ms si la música está en pausa).
//   3. en éxito se restaura setEnabled(g_volterra_enabled) -> el kernel
//      nuevo suena si Volterra estaba encendido. En fracaso se restaura
//      el estado anterior si había kernel efectivo y Volterra estaba ON
//      (el motor queda deshabilitado por el intento solo cuando el fallo
//      llegó tras el paso 2; si el fallo fue de validación, el motor ni
//      se deshabilitó: la restauración es idempotente en ambos casos).
// Hilo de CONTROL, nunca el de audio. Fail-closed: ante CUALQUIER error
// el kernel anterior queda intacto o el motor cae a bypass bit-exacto;
// nunca silencio, nunca audio corrupto.
// Devuelve el VolterraLoadStatus como int (0 = Ok). Ver
// neuromorphic/volterra_kernel_package.hpp.
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeReplaceVolterraKernelPackage(
    JNIEnv* env, jobject, jbyteArray pkg) {
    if (pkg == nullptr) {
        LOGE("[IvannaNativeLib] nativeReplaceVolterraKernelPackage: pkg null");
        return static_cast<jint>(ivanna::dsp::VolterraLoadStatus::NullArgument);
    }
    const jsize len = env->GetArrayLength(pkg);
    if (len <= 0) {
        LOGE("[IvannaNativeLib] nativeReplaceVolterraKernelPackage: pkg vacío");
        return static_cast<jint>(ivanna::dsp::VolterraLoadStatus::Truncated);
    }
    jbyte* raw = env->GetByteArrayElements(pkg, nullptr);
    if (raw == nullptr) {
        LOGE("[IvannaNativeLib] nativeReplaceVolterraKernelPackage: no se pudo leer el array");
        return static_cast<jint>(ivanna::dsp::VolterraLoadStatus::NullArgument);
    }
    // Copia local: replaceKernelPackage valida y aplica desde el buffer.
    std::vector<std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t*>(raw),
        reinterpret_cast<const std::uint8_t*>(raw) + len);
    env->ReleaseByteArrayElements(pkg, raw, JNI_ABORT);
    ivanna::dsp::VolterraLoadPolicy prod;  // producción (GENEZY01 / MOTO_G85_GENEZY | MOTO_G85_PZ1D)
    const ivanna::dsp::VolterraLoadStatus st =
        g_volterra_engine.replaceKernelPackage(bytes.data(), bytes.size(), prod);
    if (st == ivanna::dsp::VolterraLoadStatus::Ok) {
        g_volterra_engine.setEnabled(g_volterra_enabled.load(std::memory_order_acquire));
        LOGI("[IvannaNativeLib] Volterra kernel REEMPLAZADO: id=%s "
             "hasEffectiveKernel=%d enabled=%d",
             g_volterra_engine.activeKernelDeviceId(),
             g_volterra_engine.hasEffectiveKernel() ? 1 : 0,
             g_volterra_engine.isEnabled() ? 1 : 0);
    } else {
        // Fail-closed: si el intento deshabilitó el motor y había un kernel
        // efectivo con Volterra encendido, el anterior vuelve a sonar.
        if (g_volterra_engine.hasEffectiveKernel() &&
            g_volterra_enabled.load(std::memory_order_acquire) &&
            !g_volterra_engine.isEnabled()) {
            g_volterra_engine.setEnabled(true);
        }
        LOGE("[IvannaNativeLib] Volterra kernel REEMPLAZO RECHAZADO: %s (%d) "
             "(kernel anterior intacto)",
             ivanna::dsp::volterraLoadStatusName(st), static_cast<int>(st));
    }
    return static_cast<jint>(st);
}

// v2.5.369 (selector PZ1D / Genezi): device id del kernel ACTIVO del motor
// de Ruta A ("MOTO_G85_PZ1D", "MOTO_G85_GENEZY" o "" si no hay paquete
// aplicado). Es la fuente de verdad que lee la UI: nunca se infiere del
// id persistido, que puede quedar desfasado.
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetVolterraKernelId(
    JNIEnv* env, jobject) {
    return env->NewStringUTF(g_volterra_engine.activeKernelDeviceId());
}

// ═══════════════════════════════════════════════════════════════════════════
// JNI API: Acoustic Unity Engine (v2.5.15) — interruptor y telemetría medida
// ═══════════════════════════════════════════════════════════════════════════

// Apaga/enciende la coordinación (contexto neutro cuando está apagada). También viaja como bit
// OMEGA_FLAG_UNITY_OFF por SET_USER_FLAGS (daemon) para que la Ruta B (audioserver) siga el interruptor.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetUnityEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_unity_enabled.store(on, std::memory_order_release);
    // app -> daemon -> snapshot (bit _OFF: 1 = apagado). La app solo es LECTORA del bus: publish() no llega.
    ivanna::omegaSendUserFlag("unityOff", on ? 0 : 1);
    LOGI("[IvannaNativeLib] Acoustic Unity Engine set to %d", on);
}

// ═══════════════════════════════════════════════════════════════════════════
// JNI API: Punch / impacto predictivo (v2.5.41)
// ═══════════════════════════════════════════════════════════════════════════

// amount 0..1 (0 = apagado, bypass exacto). Ruta A: en proceso. Ruta B: app -> daemon (SET_PUNCH) -> snapshot.ext_flags.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPunch(JNIEnv*, jobject, jfloat amount) {
    const float a = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f;
    g_punch.setAmount(a);                 // Ruta A (atómico: seguro desde el hilo de UI)
    ivanna::omegaSendPunch(a);            // Ruta B (audioserver, vía daemon)
}

// strength 0..1 (0 = apagado, bypass exacto). Mezcla los graves hacia el otro oido con ITD de 0,26 ms
// (altavoces a +-30 grados): reduce la separacion de graves de audifonos/DAC sin tocar centro ni agudos.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCrossfeed(JNIEnv*, jobject, jfloat strength) {
    g_crossfeed.setStrength(std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f);
}

// ORBE (Binaural Esférico de Campo Cercano), rama feature/orbe-nearfield-binaural.
// amount 0..1 (0 = apagado, bypass bit-exacto; 1.0 = ACTIVO por defecto,
// directiva explícita 2026-11-08, puerta GREEN anulada), distancia 0.15..2.0 m,
// spread 0..1.5, proximidad 0..6 dB, difuso 0..1, head-tracking on/off.
// Escriben los atomics g_orbe_* que leen la Ruta A (aquí, g_orbeRouteA) y la
// Ruta B (IvannaFusionEngine::m_orbe). La Ruta B además recibe los parámetros
// por el snapshot ABI v9 (orbe_*): la app los publica al daemon por
// SET_PERCEPTUAL_STATE y omega_apply_snapshot los reenvía vía setOrbeParams().
// En ambas rutas ORBE corre EN SERIE antes de HOLOS (ORBE → HOLOS → IVANNA → Φ).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOrbeParams(JNIEnv*, jobject,
        jfloat amount, jfloat distanceM, jfloat spread, jfloat proximityDb,
        jfloat diffuse, jboolean headTracking) {
    if (std::isfinite(amount)) g_orbe_amount.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(distanceM)) g_orbe_distance_m.store(std::clamp(distanceM, 0.15f, 2.0f), std::memory_order_relaxed);
    if (std::isfinite(spread)) g_orbe_spread.store(std::clamp(spread, 0.0f, 1.5f), std::memory_order_relaxed);
    if (std::isfinite(proximityDb)) g_orbe_proximity_db.store(std::clamp(proximityDb, 0.0f, 6.0f), std::memory_order_relaxed);
    if (std::isfinite(diffuse)) g_orbe_diffuse.store(std::clamp(diffuse, 0.0f, 1.0f), std::memory_order_relaxed);
    g_orbe_head_tracking.store(headTracking == JNI_TRUE, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOrbeEnabled(JNIEnv*, jobject, jboolean enabled) {
    g_orbe_amount.store(enabled == JNI_TRUE ? 1.0f : 0.0f, std::memory_order_relaxed);
}

// Ψ (v2.5.388): radio de cabeza personalizado (m) para ORBE — Ec. 2 del
// campo con la antropometría del oyente (PsiFieldRuntime::headRadiusM).
// La app lo llama cuando conoce la antropometría (foto + CIPIC); hasta
// entonces el atomic queda en KEMAR 0.0875 y el render es bit-idéntico.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOrbeHeadRadiusM(JNIEnv*, jobject,
        jfloat radiusM) {
    if (std::isfinite(radiusM))
        g_orbe_head_radius_m.store(std::clamp(radiusM, 0.05f, 0.15f),
                                   std::memory_order_relaxed);
}

// IVANNA (v2.5.375): mismo patrón que nativeSetOrbeParams/Enabled.
// amount 0..1 (0 = apagado, bypass bit-exacto; default 0.0 = bypass
// desde v2.5.410 por distorsión medida, antes 1.0 = activo),
// scenePull 0..1, distancia 0.5..2.0 m, head-tracking on/off.
// Escriben los atomics g_omni_* que lee la Ruta A (g_omniRouteA).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOmniParams(JNIEnv*, jobject,
        jfloat amount, jfloat scenePull, jfloat distanceM, jboolean headTracking) {
    if (std::isfinite(amount)) g_omni_amount.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(scenePull)) g_omni_scene_pull.store(std::clamp(scenePull, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(distanceM)) g_omni_distance_m.store(std::clamp(distanceM, 0.5f, 2.0f), std::memory_order_relaxed);
    g_omni_head_tracking.store(headTracking == JNI_TRUE, std::memory_order_relaxed);
    LOGI("IVANNA: amount=%.2f scenePull=%.2f distanceM=%.2f headTracking=%d",
         (double)g_omni_amount.load(std::memory_order_relaxed),
         (double)g_omni_scene_pull.load(std::memory_order_relaxed),
         (double)g_omni_distance_m.load(std::memory_order_relaxed),
         (int)g_omni_head_tracking.load(std::memory_order_relaxed));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOmniEnabled(JNIEnv*, jobject, jboolean enabled) {
    g_omni_amount.store(enabled == JNI_TRUE ? 1.0f : 0.0f, std::memory_order_relaxed);
}

// Yaw de cabeza en radianes (positivo = giro a la derecha). Lo alimenta el
// head-tracker de la app; 0 = sin rotación. Sin llamador Kotlin aún: el
// operador usa 0 hasta que la app lo alimente (ver docs/IVANNA.md).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOmniHeadPose(JNIEnv*, jobject, jfloat yawRad) {
    if (std::isfinite(yawRad)) g_omni_yaw_rad.store(std::clamp(yawRad, -3.14159265f, 3.14159265f), std::memory_order_relaxed);
}

// Audiograma: pérdidas dB a 250/500/1k/2k/4k/8k Hz por oído (jfloatArray de 6).
// Valores no finitos o fuera de [0,120] se clampan; arrays nulos o cortos no hacen nada.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetOmniAudiogram(JNIEnv* env, jobject,
        jfloatArray lossL, jfloatArray lossR) {
    if (lossL == nullptr || lossR == nullptr) return;
    if (env->GetArrayLength(lossL) < 6 || env->GetArrayLength(lossR) < 6) return;
    jfloat bufL[6], bufR[6];
    env->GetFloatArrayRegion(lossL, 0, 6, bufL);
    env->GetFloatArrayRegion(lossR, 0, 6, bufR);
    for (int i = 0; i < 6; ++i) {
        const float l = std::isfinite(bufL[i]) ? std::clamp(bufL[i], 0.0f, 120.0f) : 0.0f;
        const float r = std::isfinite(bufR[i]) ? std::clamp(bufR[i], 0.0f, 120.0f) : 0.0f;
        g_omni_audio_loss_l[i].store(l, std::memory_order_relaxed);
        g_omni_audio_loss_r[i].store(r, std::memory_order_relaxed);
    }
    g_omni_audiogram_dirty.store(true, std::memory_order_relaxed);
}

// HOLOS (v2.5.364): mismo patrón que nativeSetOrbeParams/nativeSetOrbeEnabled.
// Cableado a la UI Kotlin (IvannaNativeLib.kt) y al daemon por
// SET_PERCEPTUAL_STATE (Ruta B, snapshot ABI v10). amount=0 = bypass
// bit-exacto; 0.5 = ACTIVO por defecto (harmonics 0.0 retuneado, directiva
// explícita 2026-11-08, puerta GREEN anulada). Corre EN SERIE tras ORBE
// (ORBE → HOLOS → IVANNA → Φ), no en su lugar.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHolosParams(JNIEnv*, jobject,
        jfloat amount, jfloat distanceM, jfloat spread, jfloat proximityDb,
        jfloat diffuse, jfloat harmonics, jfloat warmth, jfloat dynamics,
        jfloat width, jfloat gamma, jfloat roomRt60, jboolean headTracking,
        jint bandCount) {
    if (std::isfinite(amount)) g_holos_amount.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(distanceM)) g_holos_distance_m.store(std::clamp(distanceM, 0.15f, 2.0f), std::memory_order_relaxed);
    if (std::isfinite(spread)) g_holos_spread.store(std::clamp(spread, 0.0f, 1.5f), std::memory_order_relaxed);
    if (std::isfinite(proximityDb)) g_holos_proximity_db.store(std::clamp(proximityDb, 0.0f, 6.0f), std::memory_order_relaxed);
    if (std::isfinite(diffuse)) g_holos_diffuse.store(std::clamp(diffuse, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(harmonics)) g_holos_harmonics.store(std::clamp(harmonics, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(warmth)) g_holos_warmth.store(std::clamp(warmth, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(dynamics)) g_holos_dynamics.store(std::clamp(dynamics, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(width)) g_holos_width.store(std::clamp(width, 0.0f, 1.5f), std::memory_order_relaxed);
    if (std::isfinite(gamma)) g_holos_gamma.store(std::clamp(gamma, 0.5f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(roomRt60)) g_holos_room_rt60.store(std::clamp(roomRt60, 0.18f, 1.8f), std::memory_order_relaxed);
    g_holos_head_tracking.store(headTracking == JNI_TRUE, std::memory_order_relaxed);
    g_holos_band_count.store((bandCount == 12) ? 12 : 24, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHolosEq(JNIEnv*, jobject,
        jfloat low, jfloat mid, jfloat high, jfloat presence,
        jfloat tiltDb, jfloat freq) {
    if (std::isfinite(low)) g_holos_eq_low.store(std::clamp(low, -12.0f, 12.0f), std::memory_order_relaxed);
    if (std::isfinite(mid)) g_holos_eq_mid.store(std::clamp(mid, -12.0f, 12.0f), std::memory_order_relaxed);
    if (std::isfinite(high)) g_holos_eq_high.store(std::clamp(high, -12.0f, 12.0f), std::memory_order_relaxed);
    if (std::isfinite(presence)) g_holos_eq_presence.store(std::clamp(presence, -12.0f, 12.0f), std::memory_order_relaxed);
    if (std::isfinite(tiltDb)) g_holos_eq_tilt_db.store(std::clamp(tiltDb, -12.0f, 12.0f), std::memory_order_relaxed);
    if (std::isfinite(freq)) g_holos_eq_freq.store(std::clamp(freq, 20.0f, 20000.0f), std::memory_order_relaxed);
}

// Recorte térmico de la Ruta A: lo publica el ThermalGovernor Kotlin en cada cambio de nivel.
// holosTier 0 Normal / 1 Warm (12 bandas, H3 off, difuso x0.5) / 2 Limited (difuso off) /
// 3 Bypass (legacy bit-exacto). spatialShed apaga ORBE y NPE (mismo criterio que la Ruta B).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHolosThermalTier(JNIEnv*, jobject,
        jint holosTier, jboolean spatialShed) {
    g_holosTierA.store(std::clamp(static_cast<int>(holosTier), 0, 3), std::memory_order_relaxed);
    g_spatialShedA.store(spatialShed == JNI_TRUE, std::memory_order_relaxed);
}

// ── Bucle cerrado neuro-acústico (v2.5.365), Ruta A ─────────────────────────
// nativeSetNeuroLoopEnabled: apagado por defecto. Al apagar, el controlador
// se resetea y la ganancia por banda vuelve a 0 (bypass bit-exacto).
// nativeNeuroLoopIngest: frames REALES de sensor (kind 2=EEG en auricular,
// 3=ABR, 4=fNIRS). kind 0/1 se rechaza en C++. timestampUs es
// CLOCK_MONOTONIC en microsegundos (Kotlin: SystemClock.uptimeMillis()*1000).
// response/coherence son FloatArray[8] en [0..1], bandas Greenwood.
// nativeNeuroLoopStatus: out[4] = {active 0/1, confidence 0..1,
// framesLearned, wet 0..1}. Sin driver de sensor enchufado, active=0
// siempre: el bucle no inventa datos.
// NOTA DE RUTA: estas JNI alimentan la Ruta A (proceso app). La Ruta B
// (IvannaFusionEngine::ingestNeuroFrame, proceso audioserver) tiene la
// misma API en C++, pero su transporte (campo en el snapshot del daemon)
// queda pendiente: hoy ningún frame llega a Ruta B por JNI.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNeuroLoopEnabled(
        JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_neuroLoopEnabledA.store(on, std::memory_order_relaxed);
    if (!on) {
        g_neuroLoopRouteA.reset();
        const std::array<float, 8> zero{};
        g_neuroBandEqRouteA.setGains(zero, 48000.0f);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeNeuroLoopIngest(
        JNIEnv* env, jobject, jint kind, jlong timestampUs,
        jfloatArray response, jfloatArray coherence, jfloat quality,
        jfloat abrWaveVLatencyMs, jfloat fnirsHbO) {
    if (!g_neuroLoopEnabledA.load(std::memory_order_relaxed)) return JNI_FALSE;
    if (kind != 2 && kind != 3 && kind != 4) return JNI_FALSE;
    if (!response || !coherence) return JNI_FALSE;
    if (env->GetArrayLength(response) < 8 ||
        env->GetArrayLength(coherence) < 8) return JNI_FALSE;
    jfloat resp[8] = {};
    jfloat coh[8] = {};
    env->GetFloatArrayRegion(response, 0, 8, resp);
    env->GetFloatArrayRegion(coherence, 0, 8, coh);
    ivanna::neuromorphic::NeuroFrame frame{};
    frame.kind = static_cast<ivanna::neuromorphic::NeuroSensorKind>(kind);
    frame.timestampUs = static_cast<std::uint64_t>(timestampUs);
    frame.quality = quality;
    frame.abrWaveVLatencyMs = abrWaveVLatencyMs;
    frame.fnirsHbO = fnirsHbO;
    for (int b = 0; b < 8; ++b) {
        frame.response[static_cast<std::size_t>(b)] = resp[b];
        frame.coherence[static_cast<std::size_t>(b)] = coh[b];
    }
    // El mismo frame real alimenta también al operador Φ (sus trims EEG
    // por banda a Γ). Cada consumidor mantiene su propio estado; sin
    // frames, ambos fallan cerrados a identidad.
    const bool okLoop = g_neuroLoopRouteA.ingest(frame);
    g_hyperRouteA.setNeuroFrame(frame);
    return okLoop ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeNeuroLoopStatus(
        JNIEnv* env, jobject) {
    jfloat v[4] = {};
    const bool active =
        g_neuroLoopEnabledA.load(std::memory_order_relaxed) &&
        g_neuroLoopRouteA.isActive(ivanna::neuromorphic::monotonicNowUs());
    v[0] = active ? 1.0f : 0.0f;
    v[1] = g_neuroLoopRouteA.confidence();
    v[2] = static_cast<float>(g_neuroLoopRouteA.framesLearned());
    v[3] = g_neuroLoopRouteA.lastWetScale();
    jfloatArray out = env->NewFloatArray(4);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, 4, v);
    return out;
}

// ── Sensor acústico real por micrófono (v2.5.369), Ruta A ─────────────────
// El micrófono del teléfono (AudioRecord, hilo de Kotlin, Android 13+)
// alimenta el sensor nativo; la referencia (la salida al altavoz) la
// empuja nativeProcessBlock al anillo SPSC, sin JNI. La corrección de
// sala en vivo (RoomCorrectionEq) se aplica en nativeProcessBlock cuando
// el bucle está habilitado (nativeSensorRoomCorrectionSetEnabled/Amount,
// apagado por defecto). Sin micrófono no hay frames válidos y la
// corrección se desvanece a 0 (fail-closed).
// nativeSensorLatestFrame: FloatArray[74] = mag[0..23], err[24..47],
// coh[48..71], level[72], valid[73] (1/0). nativeSensorFrameIndex: long.
// nativeSensorRoomCorrectionStatus: [0] enabled 0/1, [1] amount 0..1,
// [2] frame válido 0/1, [3] coherencia media 0..1.
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorInit(
        JNIEnv*, jobject, jint sampleRate) {
    const int sr = (sampleRate >= 8000 && sampleRate <= 192000) ? (int)sampleRate : 48000;
    g_acousticSensorA.prepare((float)sr);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorFeedMic(
        JNIEnv* env, jobject, jfloatArray pcm, jint n) {
    if (pcm == nullptr || n <= 0) return JNI_FALSE;
    const jsize len = env->GetArrayLength(pcm);
    if (len < n) return JNI_FALSE;
    // PCM_FLOAT directo (mismo patrón que RoomMeasurement / PlaybackCapture):
    // sin conversión, el C++ recibe float [-1,1]. En trozos (pila).
    constexpr int kChunk = 4096;
    jfloat flt[kChunk];
    int done = 0;
    while (done < (int)n) {
        const int c = std::min(kChunk, (int)n - done);
        env->GetFloatArrayRegion(pcm, done, c, flt);
        if (!g_acousticSensorA.feedMic(flt, (std::size_t)c)) return JNI_FALSE;
        done += c;
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorSetTarget(
        JNIEnv* env, jobject, jfloatArray targetDb) {
    if (targetDb == nullptr || env->GetArrayLength(targetDb) < 24) return JNI_FALSE;
    jfloat t[24] = {};
    env->GetFloatArrayRegion(targetDb, 0, 24, t);
    g_acousticSensorA.setTarget(t);
    return JNI_TRUE;
}

// nativeSensorSetEliteTarget: preset élite + control de graves para el
// sensor (modo MEDIDO). preset: 0 Harman 2018 (default), 1 Harman
// élite-lean, 2 campo difuso H&M, 3 altavoces en sala. bassTrimDb: ±3 dB.
// Espejo de HyperStateOperator::setEliteTarget: el mismo JNI
// (nativeSetHyperTarget) debe llamar a ambos cuando el operador esté
// instanciado en la Ruta A, para que ambos corrijan hacia la misma curva.
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorSetEliteTarget(
        JNIEnv*, jobject, jint preset, jfloat bassTrimDb) {
    const int k = (int)preset;
    const ivanna::spatial::EliteTargetKind kind =
        (k >= 0 && k < ivanna::spatial::kEliteTargetKindCount)
            ? static_cast<ivanna::spatial::EliteTargetKind>(k)
            : ivanna::spatial::EliteTargetKind::Harman2018;
    g_acousticSensorA.setEliteTarget(kind, (float)bassTrimDb);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorSetBassTrimDb(
        JNIEnv*, jobject, jfloat bassTrimDb) {
    g_acousticSensorA.setBassTrimDb((float)bassTrimDb);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorTargetKind(
        JNIEnv*, jobject) {
    return (jint)static_cast<int>(g_acousticSensorA.targetKind());
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorLatestFrame(
        JNIEnv* env, jobject) {
    const ivanna::spatial::SensorFrame fr = g_acousticSensorA.latestFrame();
    jfloat v[74] = {};
    for (int b = 0; b < 24; ++b) {
        v[b] = fr.magnitude_db[b];
        v[24 + b] = fr.error_db[b];
        v[48 + b] = fr.coherence[b];
    }
    v[72] = fr.level_dbfs;
    v[73] = fr.valid ? 1.0f : 0.0f;
    jfloatArray out = env->NewFloatArray(74);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, 74, v);
    return out;
}

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorFrameIndex(
        JNIEnv*, jobject) {
    return (jlong)g_acousticSensorA.framesPublished();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorRoomCorrectionSetEnabled(
        JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_sensorRoomCorrEnabledA.store(on, std::memory_order_relaxed);
    if (!on) g_sensorRoomCorrAmountA.store(0.0f, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorRoomCorrectionSetAmount(
        JNIEnv*, jobject, jfloat amount) {
    if (!std::isfinite(amount)) return;
    g_sensorRoomCorrAmountA.store(std::clamp(amount, 0.0f, 1.0f),
                                  std::memory_order_relaxed);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSensorRoomCorrectionStatus(
        JNIEnv* env, jobject) {
    jfloat v[4] = {};
    v[0] = g_sensorRoomCorrEnabledA.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    v[1] = g_sensorRoomCorrAmountA.load(std::memory_order_relaxed);
    const ivanna::spatial::SensorFrame fr = g_acousticSensorA.latestFrame();
    v[2] = fr.valid ? 1.0f : 0.0f;
    float meanCoh = 0.0f;
    for (int b = 0; b < 24; ++b) meanCoh += fr.coherence[b];
    v[3] = meanCoh / 24.0f;
    jfloatArray out = env->NewFloatArray(4);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, 4, v);
    return out;
}

// ── Hiper-Operador de Estado Φ (v2.5.369), Ruta A ─────────────────────────
// nativeSetHyperAmount(amount 0..1, preset 0..2, xtcEnabled): amount=0 es
// identidad bit-exacta; 0.5 = ACTIVO por defecto (directiva explícita
// 2026-11-08, puerta GREEN anulada). preset: 0 altavoz del teléfono,
// 1 sala Genezi (Aux), 2 cabina de auto (Aux).
// nativeSetHyperTarget(kind, bassTrimDb): curva objetivo élite por preset
// (0 Harman 2018, 1 élite-lean, 2 campo difuso H&M, 3 altavoces en sala)
// + control de graves ±3 dB. Aplica LA MISMA curva al operador (modo
// APROXIMADO) y al sensor (modo MEDIDO, vía setTarget): un solo dueño
// por modo, sin doble aplicación, sin divergencia.
// nativeHyperStatus: out[6] = {amount, mode (0 off/1 aprox/2 medido),
// preset, neuroActive 0/1, spikeRate, framesReceived}.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHyperAmount(
        JNIEnv*, jobject, jfloat amount, jint preset, jboolean xtcEnabled) {
    if (!std::isfinite(amount)) return;
    g_hyper_amount.store(std::clamp(amount, 0.0f, 1.0f),
                         std::memory_order_relaxed);
    g_hyper_preset.store(std::clamp((int)preset, 0, 2),
                         std::memory_order_relaxed);
    g_hyper_xtc.store(xtcEnabled == JNI_TRUE, std::memory_order_relaxed);
    if (amount <= 0.0f) g_hyperMeasuredA.store(false, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHyperTarget(
        JNIEnv*, jobject, jint kind, jfloat bassTrimDb) {
    const auto k = static_cast<ivanna::spatial::EliteTargetKind>(
        std::clamp((int)kind, 0, 3));
    const float trim = std::isfinite(bassTrimDb)
                           ? std::clamp(bassTrimDb, -3.0f, 3.0f)
                           : 0.0f;
    // Misma curva en ambos: el operador (modo APROXIMADO) y el sensor
    // (modo MEDIDO, su error_db ya la incluye). Espejos exactos
    // (EliteTargetCurves.hpp): un solo dueño por modo, sin duplicar.
    g_hyperRouteA.setEliteTarget(k, trim);
    g_acousticSensorA.setEliteTarget(k, trim);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeHyperStatus(
        JNIEnv* env, jobject) {
    jfloat v[6] = {};
    const auto st = g_hyperRouteA.status();
    v[0] = g_hyper_amount.load(std::memory_order_relaxed);
    v[1] = static_cast<float>(st.mode);
    v[2] = static_cast<float>(g_hyper_preset.load(std::memory_order_relaxed));
    v[3] = st.neuroActive ? 1.0f : 0.0f;
    v[4] = st.spikeRate;
    v[5] = static_cast<float>(st.framesReceived);
    jfloatArray out = env->NewFloatArray(6);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, 6, v);
    return out;
}

// IVANNA OmniField Ruta A — telemetría (hilo JNI, no audio). Contadores:
// [0] processCalls [1] blocksRendered [2] framesRendered [3] designCount
// [4] fault (0/1) [5] timedSamples (muestreo CPU 1/64) [6] timedTotalUs.
// Kotlin: `external fun nativeGetOmniTelemetry(): LongArray` en IvannaNativeLib.
// [0] wet efectivo (appliedAmount) [1] convergenceR [2] sceneAzimuthDeg
// [3] diffuseness [4] media µs/bloque de OmniField (0 si no hay muestras).
// Kotlin: `external fun nativeGetOmniState(): FloatArray` en IvannaNativeLib.
JNIEXPORT jlongArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetOmniTelemetry(JNIEnv* env, jobject) {
    jlong v[7] = {
        static_cast<jlong>(g_omniRouteA.processCalls()),
        static_cast<jlong>(g_omniRouteA.blocksRendered()),
        static_cast<jlong>(g_omniRouteA.framesRendered()),
        static_cast<jlong>(g_omniRouteA.designCount()),
        g_omniRouteA.faulted() ? 1 : 0,
        static_cast<jlong>(g_omniSampleCount.load(std::memory_order_relaxed)),
        static_cast<jlong>(g_omniSampleUs.load(std::memory_order_relaxed)),
    };
    jlongArray out = env->NewLongArray(7);
    if (!out) return nullptr;
    env->SetLongArrayRegion(out, 0, 7, v);
    return out;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetOmniState(JNIEnv* env, jobject) {
    const uint64_t sN = g_omniSampleCount.load(std::memory_order_relaxed);
    const uint64_t sUs = g_omniSampleUs.load(std::memory_order_relaxed);
    jfloat v[5] = {
        g_omniRouteA.appliedAmount(),
        g_omniRouteA.convergenceR(),
        g_omniRouteA.sceneAzimuthDeg(),
        g_omniRouteA.sceneDiffuseness(),
        (sN > 0) ? static_cast<jfloat>((double)sUs / (double)sN) : 0.0f,
    };
    jfloatArray out = env->NewFloatArray(5);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, 5, v);
    return out;
}

// [0..2] ganancia dB por banda (bajo/medio/alto), [3] actividad de ataque 0..1, [4] guarda activa 0/1, [5] latencia ms.
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetPunchTelemetry(JNIEnv* env, jobject) {
    jfloat v[ivanna::PunchEngine::kTelemetrySize] = {};
    g_punch.telemetry(v);
    jfloatArray out = env->NewFloatArray(ivanna::PunchEngine::kTelemetrySize);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, ivanna::PunchEngine::kTelemetrySize, v);
    return out;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsUnityEnabled(JNIEnv*, jobject) {
    return g_unity_enabled.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

// [0..15] medidor de lazo cerrado: corrDry, corrWet, expansionDb, hfExcessDb, crestDryDb, crestWetDb,
//         crestLossDb, exceso EXP/PHASE/CLAR/FATIGUE (dB, <0 = holgura), presupuestos
//         spatial/room/exciter/dynamics, confianza.
// [16] escena (0 neutro,1 diálogo,2 música,3 cine,4 gaming,5 ambiente) [17] prob. de voz
// [18] ensanche pedido (multiplicador) [19] duck de sala [20..23] presupuestos publicados en el
// contexto: espacial, armónico, sala, dinámica. Es el estado de la Ruta A (este proceso).
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetUnityTelemetry(JNIEnv* env, jobject) {
    constexpr int kN = 24;
    jfloat v[kN] = {};
    auto& eng = ivanna::unity::AcousticUnityEngine::instance();
    eng.loopTelemetry(v);
    const auto c = eng.getContext();
    v[16] = static_cast<float>(static_cast<int>(c.scene));
    v[17] = c.voiceProbability;
    v[18] = c.msWidenerMultiplier;
    v[19] = c.rirWetDuckFactor;
    v[20] = c.spatialBudget;
    v[21] = c.harmonicBudget;
    v[22] = c.roomReverbBudget;
    v[23] = c.dynamicsBudget;
    jfloatArray out = env->NewFloatArray(kN);
    if (!out) return nullptr;
    env->SetFloatArrayRegion(out, 0, kN, v);
    return out;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetFastRpcEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_fastrpc_enabled.store(on, std::memory_order_release);
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        if (on) snap.flags |= ivanna::OMEGA_FLAG_FASTRPC_ON;
        else    snap.flags &= ~ivanna::OMEGA_FLAG_FASTRPC_ON;
        bus.publish(snap);   // no-op en la app (solo lector del bus)
    }
    ivanna::omegaSendUserFlag("fastrpc", on ? 1 : 0);   // v2.5.16: camino real hacia la Ruta B
    LOGI("[IvannaNativeLib] Qualcomm Hexagon cDSP FastRPC Offload set to %d", on);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsFastRpcEnabled(
    JNIEnv*, jobject) {
    return g_fastrpc_enabled.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAtiEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_ati_enabled.store(on, std::memory_order_release);
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        if (on) snap.flags |= ivanna::OMEGA_FLAG_ATI_ON;
        else    snap.flags &= ~ivanna::OMEGA_FLAG_ATI_ON;
        bus.publish(snap);   // no-op en la app (solo lector del bus)
    }
    ivanna::omegaSendUserFlag("ati", on ? 1 : 0);   // v2.5.16: camino real hacia la Ruta B
    LOGI("[IvannaNativeLib] Active Transducer Inversion (ATI) set to %d", on);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsAtiEnabled(
    JNIEnv*, jobject) {
    return g_ati_enabled.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetParams(
    JNIEnv* env, jobject, jfloatArray params) {
    if (!params) return;
    jfloat* p = env->GetFloatArrayElements(params, nullptr);
    if (!p) return;
    const int n = env->GetArrayLength(params);
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        if (g_params_ui.sampleRate == 0) g_params_ui = g_params;
        if (n>=1)  g_params_ui.drive     = p[0];
        if (n>=2)  g_params_ui.wet       = p[1];
        if (n>=3)  g_params_ui.mix       = p[2];
        if (n>=4)  g_params_ui.alpha     = p[3];
        if (n>=5)  g_params_ui.beta      = p[4];
        if (n>=6)  g_params_ui.gamma     = p[5];
        if (n>=7)  g_params_ui.freq      = p[6];
        if (n>=8)  g_params_ui.resonance = p[7];
        if (n>=9)  g_params_ui.low       = p[8];
        if (n>=10) g_params_ui.mid       = p[9];
        if (n>=11) g_params_ui.high      = p[10];
        if (n>=12) g_params_ui.presence  = p[11];
        if (n>=13) g_params_ui.master    = p[12];
        g_eqBaseLow .store(std::clamp(g_params_ui.low,  -24.0f, 24.0f), std::memory_order_relaxed);
        g_eqBaseMid .store(std::clamp(g_params_ui.mid,  -24.0f, 24.0f), std::memory_order_relaxed);
        g_eqBaseHigh.store(std::clamp(g_params_ui.high, -24.0f, 24.0f), std::memory_order_relaxed);
        g_params_dirty.store(true, std::memory_order_release);
    }
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEQParams(
    JNIEnv*, jobject,
    jfloat low, jfloat mid, jfloat high, jfloat master) {
    if (!std::isfinite(low) || !std::isfinite(mid) || !std::isfinite(high)
        || !std::isfinite(master)) return;
    g_eqBaseLow .store(std::clamp((float)low,  -24.0f, 24.0f), std::memory_order_relaxed);
    g_eqBaseMid .store(std::clamp((float)mid,  -24.0f, 24.0f), std::memory_order_relaxed);
    g_eqBaseHigh.store(std::clamp((float)high, -24.0f, 24.0f), std::memory_order_relaxed);
    const float masterDb = normalizeMasterToDb(master);
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        if (g_params_ui.sampleRate == 0) g_params_ui = g_params;
        g_params_ui.low    = std::clamp((float)low,  -24.0f, 24.0f);
        g_params_ui.mid    = std::clamp((float)mid,  -24.0f, 24.0f);
        g_params_ui.high   = std::clamp((float)high, -24.0f, 24.0f);
        g_params_ui.master = masterDb;
    }
    g_eq_dirty.store(true, std::memory_order_release);
}
namespace {
constexpr float kExciterWetBase = 0.32f;
} // namespace

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPresenceDb(
    JNIEnv*, jobject, jfloat db) {
    if (!std::isfinite(db)) return;
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        if (g_params_ui.sampleRate == 0) g_params_ui = g_params;
        g_params_ui.presence = std::clamp((float)db, -12.0f, 12.0f);
    }
    g_eq_dirty.store(true, std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPerceptualGain(
    JNIEnv*, jobject, jfloat gain) {
    if (!std::isfinite(gain)) return;
    const float lin = std::clamp(gain, 0.0f, 2.0f);
    const float db  = (lin <= 0.001f) ? -60.0f
                                      : std::clamp(20.0f * std::log10(lin), -60.0f, 6.0f);
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        if (g_params_ui.sampleRate == 0) g_params_ui = g_params;
        g_params_ui.master = db;
    }
    g_eq_dirty.store(true, std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCompressorAmount(
    JNIEnv*, jobject, jfloat amount) {
    if (!std::isfinite(amount)) return;
    g_thermalCompScale.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    g_comp_dirty.store(true, std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetExciterReduction(
    JNIEnv*, jobject, jfloat reduction) {
    if (!std::isfinite(reduction)) return;
    const float r = std::clamp(reduction, 0.0f, 1.0f);
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        if (g_params_ui.sampleRate == 0) g_params_ui = g_params;
        g_params_ui.wet = kExciterWetBase * (1.0f - r);
    }
    g_params_dirty.store(true, std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialWidth(
    JNIEnv*, jobject, jfloat width) {
    if (!std::isfinite(width)) return;
    g_thermalSpatialScale.store(std::clamp(width, 0.0f, 1.0f), std::memory_order_relaxed);
    recomputeSpatialWidthLocked();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPerceptualEQ(
    JNIEnv*, jobject, jfloat lowDb, jfloat midDb, jfloat highDb) {
    if (!std::isfinite(lowDb) || !std::isfinite(midDb) || !std::isfinite(highDb)) return;
    g_eqBaseLow .store(std::clamp(lowDb,  -24.0f, 24.0f), std::memory_order_relaxed);
    g_eqBaseMid .store(std::clamp(midDb,  -24.0f, 24.0f), std::memory_order_relaxed);
    g_eqBaseHigh.store(std::clamp(highDb, -24.0f, 24.0f), std::memory_order_relaxed);
    g_eq_dirty.store(true, std::memory_order_release);
}


JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetFatigueProtection(
    JNIEnv*, jobject,
    jfloat iso,
    jfloat fatigue) {

    if (!std::isfinite(iso) || !std::isfinite(fatigue))
        return;

    g_fatigueIsoDb  .store(std::clamp(iso,     -12.0f, 12.0f), std::memory_order_relaxed);
    g_fatigueProtect.store(std::clamp(fatigue,   0.0f,  1.0f), std::memory_order_relaxed);
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance()
        .hearingEngine().setListeningSpl(std::clamp(70.0f + fatigue * 22.0f - iso * 0.8f, 45.0f, 98.0f));

    g_eq_dirty.store(true, std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeResetDSP(JNIEnv*, jobject) {
    g_voice_protect_score.store(0.0f, std::memory_order_relaxed);
    g_voice_protect_score_valid.store(false, std::memory_order_release);
    g_pd.stop_evo_thread();
    g_eq.reset(); g_comp.reset(); g_exciter.reset();
    g_widener.reset();
    g_gain.reset();
    g_safety_limiter.reset();
    g_pd.reset();
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().reset();
}
// PDEngine / NHO setters exposed to Kotlin
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAlpha(JNIEnv*,jobject,jfloat v) { g_pd.set_nho_alpha(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBeta(JNIEnv*,jobject,jfloat v)  { g_pd.set_nho_beta(v); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetGamma(JNIEnv*,jobject,jfloat v) { g_pd.set_spatial_angle(v * 90.f); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEta(JNIEnv*,jobject,jfloat v)   { g_nho_wet_eta.store(v<0.f?0.f:v>1.f?1.f:v,std::memory_order_relaxed); applyNhoWet(); }
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHarmonicGain(JNIEnv*,jobject,jfloat v) {
    if (!std::isfinite(v)) return;
    const float clampedV = std::clamp((float)v, 0.0f, 1.0f);
    g_pd.set_nho_harmonic(std::clamp(clampedV, 0.0f, 0.65f));
    // La presencia del EQ tiene su propio setter (nativeSetPresenceDb): antes
    // cada cambio armónico (MusicIntelligence, AudioStateManager...) pisaba el
    // slider PRESENCIA del usuario.
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.harmonic_gain = std::clamp((float)v, 0.0f, 1.0f);
        bus.publish(snap);
    }
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHRTFEnabled(JNIEnv*,jobject,jboolean en) {
    const bool on = (en == JNI_TRUE);
    g_pd.set_mode(on ? 2 : 1);
    g_pd.set_binaural_enabled(on);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptEnabled(JNIEnv*,jobject,jboolean en) {
    g_adaptiveUiMode.store((en == JNI_TRUE) ? 1 : 0, std::memory_order_relaxed);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNPMax(JNIEnv*,jobject,jfloat v) {
    if (!std::isfinite(v)) return;
    g_pd.nho.set_mu(std::clamp((float)v, 0.05f, 0.50f));
    g_loudness_target.store(std::clamp(-v * 36.0f, -36.0f, -6.0f), std::memory_order_relaxed);
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionGain(JNIEnv*,jobject,jint,jfloat g) {
    if (std::isfinite(g)) g_pd.spatial.set_wet(std::clamp((float)g, 0.0f, 1.0f));
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionDelay(JNIEnv*,jobject,jint,jfloat d) {
    if (std::isfinite(d)) g_pd.spatial.set_width(std::clamp(static_cast<float>(d) / 23.0f, 0.25f, 2.0f));
}
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitPILSTM(JNIEnv*,jobject) { g_pd.reset(); }
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCompressorParams(
    JNIEnv*, jobject, jfloat thresholdDb, jfloat ratio, jfloat attackMs, jfloat releaseMs) {
    g_compBaseThresholdDb.store(thresholdDb, std::memory_order_relaxed);
    g_compBaseRatio.store(ratio, std::memory_order_relaxed);
    g_compPendingAttackMs.store(attackMs, std::memory_order_relaxed);
    g_compPendingReleaseMs.store(releaseMs, std::memory_order_relaxed);
    g_comp_dirty.store(true, std::memory_order_release);
}
// NHO/Espacial (GlassCard "NHO / ESPACIAL"): ángulo en radianes, ancho directo,
// y mezcla wet del efecto espacial NHO.
// Se declaran explícitas (no reusar nativeSetGamma/nativeSetDelta, que ya
// tienen semántica normalizada [0..1]→grados heredada de la UI PI-LSTM v1).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialAngleRad(
    JNIEnv*, jobject, jfloat rad) {
    const float appliedRad = ivanna::supreme::SpatialAngleReadback::appliedRadians(rad);
    if (!std::isfinite(appliedRad)) return; // preserve last valid command on invalid input
    const float appliedDeg = appliedRad * 57.29577951308232f;
    g_pd.set_spatial_angle(appliedDeg);
    g_spatialAppliedAngleRad.store(appliedRad, std::memory_order_release);
}
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetSpatialAngleRad(JNIEnv*, jobject) {
    return g_spatialAppliedAngleRad.load(std::memory_order_acquire);
}

// ── FASE 8 paso 4: binaural real ────────────────────────────────────────
// Símbolos propios — NO reusan nativeSetSpatialWidth/nativeSetSpatialAngleRad
// (esos gobiernan CueBasedSpatial y ya tienen su propio arreglo anti-colisión
// térmico/IA de FASE 7; mezclar ambos parámetros en el mismo setter
// reproduciría exactamente ese bug para un caso nuevo).
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBinauralEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    g_pd.set_binaural_enabled(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBinauralPositionRad(
    JNIEnv*, jobject, jfloat azimuthRad, jfloat aggressiveness) {
    if (!std::isfinite(azimuthRad) || !std::isfinite(aggressiveness)) return;
    g_pd.set_binaural_position(azimuthRad * 57.29578f,
                                std::clamp(aggressiveness, 0.0f, 1.0f));
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialWidthDirect(
    JNIEnv*, jobject, jfloat width) {
    if (!std::isfinite(width)) return;
    // Base de la IA/usuario — el escritor térmico la escala, nunca la sustituye.
    g_spatialBaseWidth.store(std::clamp(width, 0.0f, 2.0f), std::memory_order_relaxed);
    recomputeSpatialWidthLocked();
}
// Read-back del ancho efectivo entregado al widener (base × escala térmica). Es el valor que
// recomputeSpatialWidthLocked() escribió en g_pd; NO es una medición de la señal de audio.
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetSpatialWidthDirect(JNIEnv*, jobject) {
    return g_spatialAppliedWidth.load(std::memory_order_acquire);
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

// ── Autoridad Canónica Única para CochlearActiveInverseEngine (Rutas A, B y C) ──
// Unifica las 3 rutas JNI competidoras (CochlearInverseViewModel, PiLstmBridge,
// NativeBridge) en un único punto de control:
//   - En Ruta A/C (proceso app): controla únicamente g_cochlearEngine (manteniendo
//     g_liveAudioPipeline.cochlearEngine() apagado para impedir doble procesado).
//   - En Ruta B (daemon + audioserver): publica en effectControlBus() y envía
//     SET_COCHLEAR_INVERSE_ENABLED / SET_COCHLEAR_INTENSITY por @omega_command_socket.
static void omegaSendCochlearToDaemon(bool enabled, float intensity) noexcept {
    {
        auto& bus = ivanna::effectControlBus();
        ivanna::OmegaDspSnapshot snap;
        uint64_t seen = 0;
        if (bus.readLatest(snap, seen)) {
            snap.cochlear_intensity = intensity;
            if (enabled) snap.flags |= ivanna::OMEGA_FLAG_COCHLEAR_ON;
            else         snap.flags &= ~ivanna::OMEGA_FLAG_COCHLEAR_ON;
            bus.publish(snap);
        }
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[160];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_COCHLEAR_INVERSE_ENABLED\",\"enabled\":%d}\n"
            "{\"action\":\"SET_COCHLEAR_INTENSITY\",\"intensity\":%.4f}",
            enabled ? 1 : 0, static_cast<double>(intensity));
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

extern "C" void ivanna_cochlear_set_enabled(bool on) noexcept {
    g_cochlearEnabled.store(on, std::memory_order_release);
    g_cochlearEngine.setEnabled(on);
    auto* activePipe = ivanna::spatial::IvannaAudioPipeline::peekActiveInstance();
    if (activePipe && activePipe != &g_liveAudioPipeline) {
        activePipe->cochlearEngine().setEnabled(on);
    } else {
        g_liveAudioPipeline.cochlearEngine().setEnabled(false);
    }
    ivanna::unified::UnifiedParamSnapshotBus::instance().setCochlearUnified(
        on, g_cochlearIntensity.load(std::memory_order_relaxed));
    omegaSendCochlearToDaemon(on, g_cochlearIntensity.load(std::memory_order_relaxed));
}

extern "C" void ivanna_cochlear_set_intensity(float intensity) noexcept {
    const float w = std::isfinite(intensity)
        ? std::clamp(intensity, 0.0f, 1.0f)
        : 0.35f;
    g_cochlearIntensity.store(w, std::memory_order_relaxed);
    g_cochlearEngine.setIntensity(w);
    auto* activePipe = ivanna::spatial::IvannaAudioPipeline::peekActiveInstance();
    if (activePipe && activePipe != &g_liveAudioPipeline) {
        activePipe->cochlearEngine().setIntensity(w);
    }
    ivanna::unified::UnifiedParamSnapshotBus::instance().setCochlearUnified(
        g_cochlearEnabled.load(std::memory_order_relaxed), w);
    omegaSendCochlearToDaemon(g_cochlearEnabled.load(std::memory_order_relaxed), w);
}

extern "C" bool ivanna_cochlear_is_active() noexcept {
    if (g_cochlearEngine.isActive() || g_cochlearEnabled.load(std::memory_order_relaxed)) {
        return true;
    }
    auto* activePipe = ivanna::spatial::IvannaAudioPipeline::peekActiveInstance();
    if (activePipe && activePipe != &g_liveAudioPipeline) {
        return activePipe->cochlearEngine().isActive();
    }
    return false;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCochlearEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    ivanna_cochlear_set_enabled(enabled == JNI_TRUE);
}

// ── Puente app→daemon para Upmixing (HOA) ────────────────────────────────
// Envia un comando JSON al socket de control del daemon (abstract namespace
// "@omega_command_socket", el mismo que bindea ivanna_daemon.cpp). El daemon
// actualiza su OmegaDspState y publica el snapshot en el OmegaControlBus
// (SHM seqlock); omega_effect.cpp lo lee en el callback de audio (RT-safe,
// lock-free) y llama fc->setUpmixingEnabled()/setImmersivity().
//
// Disenio deliberado:
//  - Fire-and-forget con timeout corto: NUNCA se llama desde el hilo de
//    audio (solo desde el hilo de UI via JNI), asi que un connect() con
//    timeout de 200 ms es aceptable aqui y NO viola las reglas RT.
//  - Si el daemon no esta (sin root / no instalado), el envio falla en
//    silencio y el atomic local sigue actualizado: la Ruta A (in-process)
//    conserva el comportamiento actual. Cero regresion sin daemon.
//  - Se mantiene el atomic local para que la UI refleje el estado al
//    instante aunque el daemon tarde un ciclo en publicar.
static void omegaSendUpmixingToDaemon(bool enabled, float immersivity) noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000}; // 200 ms
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket"; // '@' abstract -> sun_path[0]='\0'
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[160];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_UPMIXING\",\"upmixingEnabled\":%d,\"upmixingImmersivity\":%.4f}",
            enabled ? 1 : 0, static_cast<double>(immersivity));
        if (n > 0) {
            (void)::write(fd, json, static_cast<size_t>(n)); // mejor esfuerzo
        }
    }
    ::close(fd);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetIntelligentUpmixingEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool en = (enabled == JNI_TRUE);
    g_upmixing_enabled.store(en, std::memory_order_relaxed);
    // Cerrar el canal cross-process: empujar al daemon -> SHM -> audioserver.
    omegaSendUpmixingToDaemon(en, g_upmixing_immersivity.load(std::memory_order_relaxed));
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.upmixing_enabled = en ? 1u : 0u;
        bus.publish(snap);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetUpmixingImmersivity(
    JNIEnv*, jobject, jfloat immersivity) {
    if (std::isfinite(immersivity)) {
        g_upmixing_immersivity.store(immersivity, std::memory_order_relaxed);
        omegaSendUpmixingToDaemon(g_upmixing_enabled.load(std::memory_order_relaxed), immersivity);
        auto& bus = ivanna::effectControlBus();
        ivanna::OmegaDspSnapshot snap;
        uint64_t seen = 0;
        if (bus.readLatest(snap, seen)) {
            snap.upmixing_immersivity = immersivity;
            bus.publish(snap);
        }
    }
}

// ── Wave Field Synthesis: puente app→daemon (mismo patrón que Upmixing) ──
static void omegaSendWfsToDaemon(bool enabled, float spread) noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000}; // 200 ms — solo hilo UI, nunca RT
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[128];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_WFS\",\"wfsEnabled\":%d,\"wfsSpread\":%.4f}",
            enabled ? 1 : 0, static_cast<double>(spread));
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

// ── Modo AUX/altavoces: puente app→daemon (SET_AUX_FIELD), mismo patron que WFS ──
static void omegaSendAuxFieldToDaemon(bool enabled, float xoverHz, float coherence) noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000}; // 200 ms — solo hilo UI, nunca RT
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[160];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_AUX_FIELD\",\"auxEnabled\":%d,\"auxXoverHz\":%.1f,\"auxCoherence\":%.4f}",
            enabled ? 1 : 0, static_cast<double>(xoverHz), static_cast<double>(coherence));
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAuxField(
    JNIEnv*, jobject, jboolean enabled, jfloat xoverHz, jfloat coherence) {
    const bool en = (enabled == JNI_TRUE);
    const float xo = std::isfinite(xoverHz)   ? std::clamp(xoverHz, 60.0f, 200.0f) : 120.0f;
    const float co = std::isfinite(coherence) ? std::clamp(coherence, 0.0f, 1.0f)  : 1.0f;
    g_aux_field_xover_hz.store(xo, std::memory_order_relaxed);
    g_aux_field_coherence.store(co, std::memory_order_relaxed);
    g_aux_field_enabled.store(en, std::memory_order_relaxed);     // ruta A (en proceso)
    omegaSendAuxFieldToDaemon(en, xo, co);                        // ruta B (audioserver, via daemon)
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.flags = (snap.flags & ~(OMEGA_FLAG_AUX_FIELD_ON | OMEGA_FLAG_AUX_XOVER_MASK | OMEGA_FLAG_AUX_COH_MASK))
                   | omega_aux_field_encode(en, xo, co);
        bus.publish(snap);
    }
}

// ── Ruta AUX Genezi / auto (v2.5.366): preset completo en Ruta A ─────────
// preset: 0 = Genezi en habitación, 1 = habitáculo (asiento conductor LHD).
// Aplica la geometría XTC del par a la cadena 4D (fourd::setSpeakerGeometry,
// misma vía que routeAApplyXtcGeometry) y el campo AUX coherente por los
// mismos atomics + daemon + snapshot que nativeSetAuxField. No toca el
// kernel Volterra (lo carga el loader IVK1 con AuxVolterraGate) ni el wet
// HRTF (en ruta AUX el binaural no aplica).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAuxGeneziPreset(
    JNIEnv*, jobject, jint preset) {
    using namespace ivanna::spatial;
    const AuxPresetKind kind =
        (preset == static_cast<jint>(AuxPresetKind::CarCabin))
            ? AuxPresetKind::CarCabin
            : AuxPresetKind::GeneziRoom;
    const AuxRouteParams p = auxPreset(kind);
    ivanna::fourd::setSpeakerGeometry(p.xtc);
    const float xo = std::clamp(p.auxXoverHz, 60.0f, 200.0f);
    const float co = std::clamp(p.auxCoherence, 0.0f, 1.0f);
    const bool en = p.auxFieldEnabled;
    g_aux_field_xover_hz.store(xo, std::memory_order_relaxed);
    g_aux_field_coherence.store(co, std::memory_order_relaxed);
    g_aux_field_enabled.store(en, std::memory_order_relaxed);
    omegaSendAuxFieldToDaemon(en, xo, co);
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.flags = (snap.flags & ~(OMEGA_FLAG_AUX_FIELD_ON | OMEGA_FLAG_AUX_XOVER_MASK | OMEGA_FLAG_AUX_COH_MASK))
                   | omega_aux_field_encode(en, xo, co);
        bus.publish(snap);
    }
}

// ── Corrección acústica de sala: puente app→daemon (SET_ROOM_CORRECTION) + medición ──
static void omegaSendRoomCorrectionToDaemon(const float* g) noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[320];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_ROOM_CORRECTION\",\"roomCorrDb\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]}",
            (double)g[0], (double)g[1], (double)g[2], (double)g[3], (double)g[4],
            (double)g[5], (double)g[6], (double)g[7], (double)g[8], (double)g[9]);
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetRoomCorrection(
    JNIEnv* env, jobject, jfloatArray gainsDb) {
    float g[10] = {};
    if (gainsDb != nullptr && env->GetArrayLength(gainsDb) >= 10) {
        env->GetFloatArrayRegion(gainsDb, 0, 10, g);
        for (int i = 0; i < 10; ++i) g[i] = std::isfinite(g[i]) ? std::clamp(g[i], -12.0f, 12.0f) : 0.0f;
    }
    for (int i = 0; i < 10; ++i) g_room_corr_db[i].store(g[i], std::memory_order_relaxed);   // ruta A (en proceso)
    omegaSendRoomCorrectionToDaemon(g);                                                       // ruta B (audioserver, via daemon)
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        for (int i = 0; i < 10; ++i) snap.room_corr_db[i] = g[i];
        bus.publish(snap);
    }
}

// ── Motor NPE neuromórfico en el módulo (Ruta B): app→daemon (SET_NPE) ──
// Devuelve true SOLO si conectó con el daemon y escribió el comando completo (antes se ignoraba el resultado
// y la app no podía saber que el módulo se había quedado con sus valores por defecto).
static bool omegaSendNpeToDaemon(bool enabled, float lateral, float ohc, float masterDb) noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    bool ok = false;
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[200];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_NPE\",\"enabled\":%d,\"lateralInhib\":%.3f,\"ohc\":%.3f,\"masterDb\":%.2f}",
            enabled ? 1 : 0, (double)lateral, (double)ohc, (double)masterDb);
        if (n > 0) ok = (::write(fd, json, static_cast<size_t>(n)) == static_cast<ssize_t>(n));
    }
    ::close(fd);
    return ok;
}

/// Diafonía: la cadena 4D de la app (ivanna_4d_jni.cpp) la enciende en la Ruta A; esto la sincroniza con el módulo.
extern "C" void ivanna_send_crosstalk_to_daemon(bool on) noexcept {
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        if (on) snap.temporal_field |= OMEGA_TEMPORAL_FIELD_XTC_BIT;
        else    snap.temporal_field &= ~OMEGA_TEMPORAL_FIELD_XTC_BIT;
        bus.publish(snap);
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[96];
        int n = std::snprintf(json, sizeof(json), "{\"action\":\"SET_CROSSTALK\",\"enabled\":%d}", on ? 1 : 0);
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

/** Publica el estado del NPE al módulo (omega_effect en audioserver). La Ruta A usa IvannaNpeEngine en la app. */
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetNpeModule(
    JNIEnv*, jobject, jboolean enabled, jfloat lateralInhib, jfloat ohcCompression, jfloat masterDb) {
    return omegaSendNpeToDaemon(enabled == JNI_TRUE,
                         std::isfinite(lateralInhib) ? std::clamp(lateralInhib, 0.0f, 1.0f) : 0.2f,
                         std::isfinite(ohcCompression) ? std::clamp(ohcCompression, 0.0f, 1.0f) : 0.3f,
                         std::isfinite(masterDb) ? std::clamp(masterDb, -24.0f, 12.0f) : 0.0f)
               ? JNI_TRUE : JNI_FALSE;
}

/**
 * Prueba de NULL del motor NPE: ¿cuánto AÑADE de verdad? Procesa 2 s de una señal sintética tipo música (grave con
 * ataques + medios + agudos) por NpeStereoCore (el mismo núcleo que corre en el módulo) apagado (A) y encendido (B)
 * con los parámetros dados, alinea por retardo y mide el residuo por 5 bandas (NeuromorphicAbHarness).
 * Devuelve [residuo global dB, cambio de ganancia dB, coherencia, bandas audibles, delta dB de 5 bandas...] (9 valores).
 * Señal sintética: orienta, no sustituye una escucha A/B.
 */
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeNpeAbNull(
    JNIEnv* env, jobject, jfloat harmonicGain, jfloat lateralInhib, jfloat ohcCompression, jfloat masterDb) {
    constexpr int kN = 96000, kBlock = 1024;
    constexpr float kSr = 48000.0f, kTwoPi = 6.28318530718f;
    std::vector<float> aL(kN), aR, bL, bR;
    for (int i = 0; i < kN; ++i) {
        const float t = static_cast<float>(i) / kSr;
        const float beat = std::exp(-std::fmod(t, 0.5f) * 9.0f);            // ataque cada 0.5 s
        aL[i] = 0.35f * beat * std::sin(kTwoPi * 60.0f * t) + 0.16f * std::sin(kTwoPi * 440.0f * t)
              + 0.10f * std::sin(kTwoPi * 2500.0f * t) + 0.04f * std::sin(kTwoPi * 9000.0f * t);
    }
    aR = aL; bL = aL; bR = aL;
    ivanna::neuromorphic::NpeStereoCore core;
    core.prepare(kSr);
    core.setParams(true, harmonicGain, lateralInhib, ohcCompression, masterDb);
    for (int k = 0; k + kBlock <= kN; k += kBlock) core.process(bL.data() + k, bR.data() + k, kBlock);
    ivanna::measure::AbHarness h;
    h.prepare(kSr);
    const ivanna::measure::AbReport r = h.measure(aL.data(), bL.data(), kN);
    float out[9] = {r.residualRmsDb, r.gainDb, r.coherence, static_cast<float>(r.audibleBands),
                    r.bands[0].deltaDb, r.bands[1].deltaDb, r.bands[2].deltaDb, r.bands[3].deltaDb, r.bands[4].deltaDb};
    jfloatArray arr = env->NewFloatArray(9);
    if (arr != nullptr) env->SetFloatArrayRegion(arr, 0, 9, out);
    return arr;
}

/** Amplitud (lineal, pico) de la componente a `freqHz` en samples[0..count): detección síncrona (RoomProbe). */
JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRoomLockIn(
    JNIEnv* env, jobject, jfloatArray samples, jint count, jint sampleRate, jfloat freqHz) {
    if (samples == nullptr || count <= 0) return 0.0f;
    const jsize have = env->GetArrayLength(samples);
    const jint n = std::min<jint>(count, have);
    jfloat* p = env->GetFloatArrayElements(samples, nullptr);
    if (p == nullptr) return 0.0f;
    const float a = ivanna::room::lockInAmplitude(p, static_cast<std::size_t>(n), static_cast<float>(sampleRate), freqHz);
    env->ReleaseFloatArrayElements(samples, p, JNI_ABORT);
    return a;
}

/** Diseña la corrección (10 ganancias en dB) a partir de 27 niveles (dB) y 27 SNR (dB). Vacío si no hay referencia. */
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRoomDesign(
    JNIEnv* env, jobject, jfloatArray levelDb, jfloatArray snrDb, jint sampleRate) {
    if (levelDb == nullptr || snrDb == nullptr ||
        env->GetArrayLength(levelDb) < ivanna::room::kProbePoints || env->GetArrayLength(snrDb) < ivanna::room::kProbePoints)
        return env->NewFloatArray(0);
    float lv[ivanna::room::kProbePoints], sn[ivanna::room::kProbePoints], out[ivanna::room::kBands];
    env->GetFloatArrayRegion(levelDb, 0, ivanna::room::kProbePoints, lv);
    env->GetFloatArrayRegion(snrDb, 0, ivanna::room::kProbePoints, sn);
    if (!ivanna::room::designCorrection(lv, sn, static_cast<float>(sampleRate), out)) return env->NewFloatArray(0);
    jfloatArray arr = env->NewFloatArray(ivanna::room::kBands);
    if (arr != nullptr) env->SetFloatArrayRegion(arr, 0, ivanna::room::kBands, out);
    return arr;
}

/** Render 3D de la geometría real de sala a PPM 4K. Solo escribe un nombre fijo en el directorio privado recibido. */
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRenderRoomGeometry(
    JNIEnv* env, jobject, jstring privateDirectory, jint width, jint height) {
    if (privateDirectory == nullptr || width <= 0 || height <= 0 ||
        width > static_cast<jint>(ivanna::spatial::render::SceneGeometryRenderer::k4kWidth) ||
        height > static_cast<jint>(ivanna::spatial::render::SceneGeometryRenderer::k4kHeight)) return JNI_FALSE;
    const char* raw = env->GetStringUTFChars(privateDirectory, nullptr);
    if (raw == nullptr) return JNI_FALSE;
    const std::string dir(raw);
    env->ReleaseStringUTFChars(privateDirectory, raw);
    if (dir.empty() || dir.front() != '/' || dir.find("..") != std::string::npos) return JNI_FALSE;
    const std::string output = dir + "/ivanna_room_geometry.ppm";
    const auto scene = ivanna::spatial::render::fromRoomGeometryConfig(
        ivanna::spatial::RoomGeometryConfig::defaultLayout());
    return ivanna::spatial::render::SceneGeometryRenderer::renderPpm(
        scene, output, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)) ? JNI_TRUE : JNI_FALSE;
}

// ── Dinamica espacial temporal ("4D"): puente app→daemon (SET_TEMPORAL_FIELD) ──
static void omegaSendTemporalFieldToDaemon(bool enabled, float amount, bool autoMode) noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) == 0) {
        char json[128];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_TEMPORAL_FIELD\",\"temporalEnabled\":%d,\"temporalAmount\":%.4f,\"temporalAuto\":%d}",
            enabled ? 1 : 0, static_cast<double>(amount), autoMode ? 1 : 0);
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetTemporalField(
    JNIEnv*, jobject, jboolean enabled, jfloat amount, jboolean autoMode) {
    const bool en = (enabled == JNI_TRUE);
    const bool au = (autoMode == JNI_TRUE);
    const float am = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.6f;
    g_temporal_field_amount.store(am, std::memory_order_relaxed);
    g_temporal_field_auto.store(au, std::memory_order_relaxed);
    g_temporal_field_enabled.store(en, std::memory_order_relaxed);   // ruta A (en proceso)
    omegaSendTemporalFieldToDaemon(en, am, au);                          // ruta B (audioserver, via daemon)
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.temporal_field = (snap.temporal_field & OMEGA_TEMPORAL_FIELD_XTC_BIT)
                            | omega_temporal_field_encode(en, am, au);
        bus.publish(snap);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetWfsEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool en = (enabled == JNI_TRUE);
    g_wfs_enabled.store(en, std::memory_order_relaxed);
    omegaSendWfsToDaemon(en, g_wfs_spread.load(std::memory_order_relaxed));
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.wfs_enabled = en ? 1u : 0u;
        bus.publish(snap);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetWfsSpread(
    JNIEnv*, jobject, jfloat spread) {
    if (std::isfinite(spread)) {
        const float sp = std::clamp(spread, 0.0f, 2.0f);
        g_wfs_spread.store(sp, std::memory_order_relaxed);
        g_routeAWfsEngine.setSpread(sp);
        omegaSendWfsToDaemon(g_wfs_enabled.load(std::memory_order_relaxed), sp);
        auto& bus = ivanna::effectControlBus();
        ivanna::OmegaDspSnapshot snap;
        uint64_t seen = 0;
        if (bus.readLatest(snap, seen)) {
            snap.wfs_spread = sp;
            bus.publish(snap);
        }
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetWfsSpeakerLayout(
    JNIEnv* env, jobject, jfloatArray jx, jfloatArray jy, jfloatArray jz) {
    // Reject malformed layouts before either shared-memory publication or IPC.
    // Publishing NaN/Inf to the seqlock first would poison the live DSP snapshot
    // even if the daemon later rejected the JSON payload.
    if (!jx || !jy || !jz) return;
    if (env->GetArrayLength(jx) != 7 ||
        env->GetArrayLength(jy) != 7 ||
        env->GetArrayLength(jz) != 7) return;
    jfloat x[7], y[7], z[7];
    env->GetFloatArrayRegion(jx, 0, 7, x);
    env->GetFloatArrayRegion(jy, 0, 7, y);
    env->GetFloatArrayRegion(jz, 0, 7, z);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    // These arrays are room-frame coordinates translated into the native fixed-listener
    // frame by WfsCalibrationManager. A listener near a room boundary or a yaw rotation
    // can legitimately place virtual speakers outside the physical room bounds; reject
    // non-finite/extreme values, but do not silently drop valid translated geometry.
    for (int i = 0; i < 7; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || !std::isfinite(z[i]) ||
            std::fabs(x[i]) > 7.0f || std::fabs(y[i]) > 5.0f ||
            std::fabs(z[i]) > 9.0f) return;
    }

    g_routeAWfsEngine.setSpeakerLayout(x, y, z);
    routeAApplyXtcGeometry(x, y, z);

    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        for (int i = 0; i < 7; ++i) {
            snap.wfs_speaker_x[i] = x[i];
            snap.wfs_speaker_y[i] = y[i];
            snap.wfs_speaker_z[i] = z[i];
        }
        bus.publish(snap);
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr; std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char* name = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, std::strlen(name));
    socklen_t slen = offsetof(struct sockaddr_un, sun_path) + 1 + std::strlen(name);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), slen) == 0) {
        char json[512];
        int n = std::snprintf(json, sizeof(json),
            "{\"action\":\"SET_WFS_LAYOUT\","
            "\"wfsSpeakerX\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],"
            "\"wfsSpeakerY\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],"
            "\"wfsSpeakerZ\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}",
            x[0],x[1],x[2],x[3],x[4],x[5],x[6],
            y[0],y[1],y[2],y[3],y[4],y[5],y[6],
            z[0],z[1],z[2],z[3],z[4],z[5],z[6]);
        if (n > 0) (void)::write(fd, json, static_cast<size_t>(n));
    }
    ::close(fd);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAntiDolbyIntensity(
    JNIEnv*, jobject, jfloat v) {
    if (!std::isfinite(v)) return;
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    g_nho_wet_spatial.store(clamped, std::memory_order_relaxed);
    applyNhoWet();

    // Propagar a través del bus de control seqlock para Route B (omega_effect en audioserver)
    auto& bus = ivanna::effectControlBus();
    ivanna::OmegaDspSnapshot snap;
    uint64_t seen = 0;
    if (bus.readLatest(snap, seen)) {
        snap.anti_dolby = clamped;
        if (clamped > 0.01f) snap.flags |= ivanna::OMEGA_FLAG_ANTI_DOLBY_ON;
        else                 snap.flags &= ~ivanna::OMEGA_FLAG_ANTI_DOLBY_ON;
        bus.publish(snap);
    }
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAntiDolbyIntensity(JNIEnv*, jobject) {
    // Read-back of the command sent to the DSP wet path; this is not measured output audio.
    return g_nho_wet_spatial.load(std::memory_order_relaxed);
}

static std::atomic<float> g_spsc_ring_factor{1.0f};

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpscRingFactor(
    JNIEnv*, jobject, jfloat factor) {
    if (!std::isfinite(factor)) return;
    g_spsc_ring_factor.store(std::clamp(factor, 0.5f, 2.0f), std::memory_order_relaxed);
    LOGI("[IvannaNativeLib] Anti-Dolby SPSC Ring Buffer factor set to %.2f", factor);
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetSpscRingFactor(
    JNIEnv*, jobject) {
    return g_spsc_ring_factor.load(std::memory_order_relaxed);
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
// El toggle manual y el watchdog usan el mismo contrato serializado. El
// handle solo se entrega cuando existe un hilo de control realmente vivo.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptiveEngineEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    (void)setAdaptiveEngineEnabled(enabled == JNI_TRUE);
}
JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeCreateAdaptiveEngine(
    JNIEnv*, jobject) {
    if (!ensureAdaptiveEngineStarted()) return 0;
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
    // Estado del hilo de control, no actividad de audio. El contador de
    // bloques aplicados se expone aparte y puede permanecer quieto durante
    // silencio, pausa o ausencia de una sesión AudioFlinger.
    const bool enabled = g_adaptiveEngineUiEnabled.load(std::memory_order_acquire);
    const bool running = g_adaptiveEngine.running();
    g_adaptiveEngineStarted.store(running, std::memory_order_release);
    return (enabled && running) ? JNI_TRUE : JNI_FALSE;
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
// ── nativeGetEffectAmounts — amounts de los 4 efectos principales ───────
// Telemetría en vivo 1 Hz (v2.5.407, IVANNA.Telemetry): una sola llamada
// JNI por segundo en vez de cuatro. FloatArray[4]:
//   [0] orbe     (g_orbe_amount, 0..1; 1.0 activo por defecto, directiva 2026-11-08)
//   [1] holos    (g_holos_amount, 0..1; 0.5 activo por defecto, directiva 2026-11-08)
//   [2] phi      (g_hyper_amount, 0..1; 0.5 activo por defecto, directiva 2026-11-08)
//   [3] volterra (g_volterra_enabled, 1.0/0.0; no tiene amount, solo on/off)
// Todos son atómicos del proceso app (orbe/holos definidos en sus
// *_controls_bridge.cpp del target ivanna_omega; hyper/volterra, estáticos
// en este archivo). Solo lectura, sin locks, seguro desde el poller.
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetEffectAmounts(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(4);
    if (!arr) return nullptr;
    const float v[4] = {
        g_orbe_amount.load(std::memory_order_relaxed),
        g_holos_amount.load(std::memory_order_relaxed),
        g_hyper_amount.load(std::memory_order_relaxed),
        g_volterra_enabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f
    };
    env->SetFloatArrayRegion(arr, 0, 4, v);
    return arr;
}

// ── nativeGetDspShed — estado del gobernador de shed (v2.5.410) ──────────
// Telemetría en vivo 1 Hz (IVANNA.Telemetry): el shed es el dato que
// confirma si la cadena alcanza el periodo en el teléfono. FloatArray[3]:
//   [0] shedFraction (0..1; 0.034 = 3.4% de bloques en passthrough)
//   [1] emaUs        (coste medio DSP por bloque procesado, µs)
//   [2] peakUs       (peor bloque procesado visto, µs)
// Solo lectura de la instancia estática del gobernador (sin locks, sin
// I/O); seguro desde el poller de fondo. Antes de la 410 el shed solo se
// veía en las líneas throttled "DSP shed Ruta A ..." del hilo de audio;
// con este getter la telemetría 1 Hz lo lleva sin loguear en RT.
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetDspShed(
    JNIEnv* env, jobject) {
    jfloatArray arr = env->NewFloatArray(3);
    if (!arr) return nullptr;
    const float v[3] = {
        static_cast<float>(g_dspShedGovernor.shedFraction()),
        static_cast<float>(g_dspShedGovernor.emaUs()),
        static_cast<float>(g_dspShedGovernor.peakUs())
    };
    env->SetFloatArrayRegion(arr, 0, 3, v);
    return arr;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAdaptiveLambdaT(
    JNIEnv*, jobject) {
    return g_adaptiveEngine.getAdaptiveLambdaT();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptiveEnvironmentRT60(
    JNIEnv*, jobject, jfloat rt60) {
    g_adaptiveEngine.setEnvironmentRT60(static_cast<float>(rt60));
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
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
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

    // 3. Parar y drenar siempre el hilo de control del AdaptiveDecisionEngine.
    // No confiar solo en g_adaptiveEngineStarted: una ruta de recuperación
    // fallida puede dejar el std::thread joinable aunque el flag esté en false.
    (void)setAdaptiveEngineEnabled(false);

    // 3. Parar el worker de carga de IR Ruta A — accede a g_rirDataset/
    //    g_rirConvolver (estáticos) y no debe sobrevivir a dlclose().
    g_rirWorkerRunning.store(false, std::memory_order_release);
    g_rirWorkerCv.notify_all();
    if (g_rirWorkerThread.joinable()) g_rirWorkerThread.join();
}

// ── RIR dataset: metadata + fallback de directorio (2026-09-19) ─────────────
// La pantalla Acustica SAF·RIR necesita listar las 200 salas y mostrar estado
// real. El dataset vive en /data/adb/ivanna_omega/rir (root) o, sin root, en
// filesDir/rir extraido de los assets empaquetados (rir_0000.wav..rir_0199.wav
// + metadata.csv YA van dentro del APK). nativeSetRirDataDir re-inicializa el
// dataset desde ese directorio cuando la ruta root no existe. Todo corre en
// hilo de UI; el hot path de audio sigue leyendo g_rirDataset con acquire.
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativeGetLocalRirTailStatus(JNIEnv*, jclass) {
    // -1 = convolver local no inicializado; 0 = sin degradación detectada;
    // 1 = la cola retrasada se omitió por superar el presupuesto algorítmico.
    Ivanna::RirConvolver* conv = g_rirConvolver.load(std::memory_order_acquire);
    if (!conv) return static_cast<jint>(-1);
    return conv->tailLatencyBudgetExceeded() ? static_cast<jint>(1) : static_cast<jint>(0);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativeGetRirRoomCount(JNIEnv*, jclass) {
    Ivanna::RirDataset* ds = g_rirDataset.load(std::memory_order_acquire);
    return (ds && ds->isLoaded()) ? (jint)ds->roomCount() : 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativeIsRirDatasetLoaded(JNIEnv*, jclass) {
    Ivanna::RirDataset* ds = g_rirDataset.load(std::memory_order_acquire);
    return (ds && ds->isLoaded()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativeGetRirRoomInfo(JNIEnv* env, jclass, jint idx) {
    Ivanna::RirDataset* ds = g_rirDataset.load(std::memory_order_acquire);
    if (!ds || !ds->isLoaded() || idx < 0 || (size_t)idx >= ds->roomCount()) return nullptr;
    const Ivanna::RirRoomMeta& m = ds->meta((size_t)idx);
    // Formato compacto: nombre|rt60_nominal_s|volumen_m3|distancia_m|WxHxD|rt60_medido_T20_s
    // (6o campo agregado en v2.5.197; 0.000 = columna ausente en metadata.csv antiguo)
    char buf[352];
    std::snprintf(buf, sizeof(buf), "%s|%.3f|%.1f|%.2f|%.1fx%.1fx%.1f|%.3f",
                  m.filename.c_str(), (double)m.rt60S, (double)m.volumeM3(),
                  (double)m.distanceM, (double)m.roomWidthM, (double)m.roomHeightM,
                  (double)m.roomDepthM, (double)m.rt60MeasuredS);
    return env->NewStringUTF(buf);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativeSetRirDataDir(JNIEnv* env, jclass, jstring jdir) {
    if (!jdir) return JNI_FALSE;
    Ivanna::RirDataset* cur = g_rirDataset.load(std::memory_order_acquire);
    if (cur && cur->isLoaded()) return JNI_TRUE;   // ya cargado (root): no pisar
    const char* d = env->GetStringUTFChars(jdir, nullptr);
    if (!d) return JNI_FALSE;
    Ivanna::RirDataset* ds = new Ivanna::RirDataset();
    bool okds = ds->load(d);
    env->ReleaseStringUTFChars(jdir, d);
    if (!okds) { delete ds; return JNI_FALSE; }
    // Publicar. El dataset previo (nulo o no cargado) se abandona sin delete:
    // init ocurre una sola vez por proceso y el worker pudo tomar el puntero.
    g_rirDataset.store(ds, std::memory_order_release);
    if (g_rirConvolver.load(std::memory_order_acquire) == nullptr) {
        auto* conv = new Ivanna::RirConvolver();
        conv->applySofaCoupling(ivanna::master::kMasterSafGoldenQ);
        conv->setTrueStereoCrossGain(0.20f);
        // The RIR tail crossmix is experimental, not a measured transaural inverse.
        conv->setTransauralXtcStrength(0.0f);
        const float initRt60 = g_localRoomRt60.load(std::memory_order_acquire);
        const float initWet  = g_localRoomWet.load(std::memory_order_acquire);
        if (initRt60 >= 0.01f && initWet > 0.001f) {
            conv->setWetDry(initWet);
            const int32_t reqIdx = g_localRoomIdx.load(std::memory_order_acquire);
            const int32_t targetIdx = (reqIdx >= 0 && static_cast<size_t>(reqIdx) < ds->roomCount())
                ? reqIdx
                : static_cast<int32_t>(ds->findNearestSmart(initRt60));
            g_rirPendingIdx.store(targetIdx, std::memory_order_release);
        } else {
            conv->setWetDry(0.0f);
        }
        g_rirConvolver.store(conv, std::memory_order_release);
    }
    if (!g_rirWorkerRunning.exchange(true, std::memory_order_acq_rel))
        g_rirWorkerThread = std::thread(rirWorkerLoop);
    if (g_localRoomRt60.load(std::memory_order_acquire) >= 0.01f &&
        g_localRoomWet.load(std::memory_order_acquire) > 0.001f) {
        g_rirWorkerCv.notify_one();
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativeSetLocalRoom(
    JNIEnv*, jclass, jfloat rt60S, jfloat wet, jint roomIdx) {
    const bool active = (rt60S > 0.005f && wet > 0.0001f);
    g_localRoomRt60.store(active ? static_cast<float>(rt60S) : 0.0f, std::memory_order_release);
    g_localRoomWet.store(active ? static_cast<float>(wet) : 0.0f, std::memory_order_release);
    g_localRoomIdx.store(active ? static_cast<int32_t>(roomIdx) : -1, std::memory_order_release);
    g_adaptiveEngine.setEnvironmentRT60(active ? static_cast<float>(rt60S) : 0.30f);
    {
        auto& pipe = ivanna::spatial::IvannaAudioPipeline::getActiveInstance();
        pipe.roomEngine().setProjectionWet(active ? std::clamp(static_cast<float>(wet) * 0.45f, 0.0f, 0.45f) : 0.0f);
        pipe.roomEngine().setInversionGain(active ? std::clamp(0.25f + 0.25f * static_cast<float>(rt60S), 0.15f, 0.65f) : 0.20f);
        pipe.physicalScene().setWallAbsorption(active ? std::clamp(1.0f / (1.0f + 1.8f * static_cast<float>(rt60S)), 0.12f, 0.88f) : 0.30f);
    }

    // Sincronizar también el OmegaControlBus local para que DSPBridge_nativeProcess
    // (que lee effectControlBus().readLatest) no re-active una sala vieja.
    {
        auto& bus = ivanna::effectControlBus();
        ivanna::OmegaDspSnapshot snap;
        uint64_t seen = 0;
        if (bus.readLatest(snap, seen)) {
            snap.room_rt60_s = active ? static_cast<float>(rt60S) : 0.0f;
            snap.room_wet    = active ? std::clamp(static_cast<float>(wet), 0.0f, 1.0f) : 0.0f;
            snap.room_idx    = active ? static_cast<int32_t>(roomIdx) : -1;
            bus.publish(snap);
        }
    }

    Ivanna::RirDataset*   ds   = g_rirDataset.load(std::memory_order_acquire);
    Ivanna::RirConvolver* conv = g_rirConvolver.load(std::memory_order_acquire);
    if (!active) {
        g_rirPendingIdx.store(-1, std::memory_order_release);
        if (conv) {
            conv->setWetDry(0.0f);
            conv->unload();
        }
        return;
    }
    if (!conv) return;
    conv->applySofaCoupling(g_localSafQ);
    conv->setTrueStereoCrossGain(0.20f);
    // Room RT60 selects the room model, never a loudspeaker crosstalk-cancellation amount.
    conv->setTransauralXtcStrength(0.0f);
    conv->setWetDry(std::clamp(static_cast<float>(wet), 0.0f, 1.0f));
    if (ds && ds->isLoaded() && ds->roomCount() > 0) {
        const int32_t targetIdx = (roomIdx >= 0 && static_cast<size_t>(roomIdx) < ds->roomCount())
            ? static_cast<int32_t>(roomIdx)
            : static_cast<int32_t>(ds->findNearestSmart(rt60S));
        g_rirPendingIdx.store(targetIdx, std::memory_order_release);
        g_rirWorkerCv.notify_one();
    } else if (conv->needsMasterStudioBrirSynthesis(rt60S, static_cast<int>(g_params.sampleRate))) {
        g_rirPendingSynthRt60.store(rt60S, std::memory_order_release);
        g_rirWorkerCv.notify_one();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_magisk_OmegaEngineBridge_nativePushLocalSafLatent(
    JNIEnv* env, jclass, jfloatArray qArr, jfloat gain) {
    if (!qArr) return;
    const jsize len = env->GetArrayLength(qArr);
    if (len < 7) return;
    float q[7]{};
    env->GetFloatArrayRegion(qArr, 0, 7, q);
    const float g = std::clamp(static_cast<float>(gain), 0.0f, 2.0f);
    for (int i = 0; i < 7; ++i) {
        g_localSafQ[i] = (g > 0.01f) ? (q[i] * g) : ivanna::master::kMasterSafGoldenQ[i];
    }
    if (Ivanna::RirConvolver* conv = g_rirConvolver.load(std::memory_order_acquire)) {
        conv->applySofaCoupling(g_localSafQ);
    }
}


