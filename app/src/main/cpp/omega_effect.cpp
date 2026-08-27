#include <jni.h>
#include <android/log.h>
#include "IvannaFusionCore.cpp"
#include "spatial/RirConvolver.hpp"
#include "spatial/RirDataset.hpp"
#include <vector>
#include "audio_effect_compat.h"
#include "include/omega_control_bus.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <atomic>
#include <algorithm>   // AUDIT FIX #4: std::clamp / std::isfinite en SET_PARAM
#include "include/SafetyLimiter.h"  // FIX distorsion: limiter de Ruta A reusado en Ruta B
#include <cmath>
#include <mutex>
#include <condition_variable>
#include <thread>

// IvannaFusionCore = IvannaFusionEngine (alias en IvannaFusionCore.hpp).
// using namespace evita cualificar con Ivanna:: en todo el archivo.
using namespace Ivanna;

#define LOG_TAG "IvannaOmegaEffect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
// FIX (trazabilidad HRTF): sin este canal no habia forma de saber, en un
// dispositivo real, si el motor estaba usando el dataset MEDIDO o habia
// caido al sintetico — el fallback era completamente silencioso.
#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#endif

// ── AUDIT FIX #4 (plano de control) ──────────────────────────────────────────
// EFFECT_CMD_SET_PARAM era un no-op: cualquier android.media.audiofx.
// AudioEffect.setParameter() enviado por la app se descartaba en silencio.
// El bus cross-process (OmegaControlBus, seqlock SHM) YA existía y YA está
// enganchado a omega_process/omega_apply_snapshot — pero solo lo alimentaba
// el daemon (command_server.publish). En un dispositivo sin root ni daemon,
// el bus quedaba mudo y omega_process corría con los defaults del core.
//
// Este bloque cierra el ciclo SIN romper nada:
//   1. El efecto abre un writer del bus (path per-proceso, no colisiona con
//      el del daemon) SOLO si openReader() del daemon falló, evitando dos
//      writers concurrentes sobre la misma SHM.
//   2. Cuando llega SET_PARAM, el efecto:
//        a) Aplica el parámetro directamente al ctx->fusionCore (no se
//           pierde antes del próximo readLatest()).
//        b) Publica un OmegaDspSnapshot al bus (writer local, o al daemon
//           via bus si el daemon corre — en ese caso la app debería usar
//           su LocalSocket, pero si por lo que sea llega aquí, aplicamos
//           local y ya está).
//   3. IDs de parámetro: rango proprietary 0x00010000+, nunca colisiona
//      con IDs AOSP reservados (0..0x00FFFFFF están reservados pero AOSP
//      no reclama subrangos concretos para efectos custom con UUID propio).
// ─────────────────────────────────────────────────────────────────────────────
enum omega_effect_param_id : uint32_t {
    OMEGA_PARAM_INTENSITY           = 0x00010001, // f32 [0..1]
    OMEGA_PARAM_SPATIAL_WIDTH       = 0x00010002, // f32 [0..3]
    OMEGA_PARAM_HARMONIC_GAIN       = 0x00010003, // f32 [0..2]
    OMEGA_PARAM_COMPRESSOR_THRESHOLD= 0x00010004, // f32 (dB, <0)
    OMEGA_PARAM_COMPRESSOR_AMOUNT   = 0x00010005, // f32 [0..1]
    OMEGA_PARAM_LOUDNESS_TARGET     = 0x00010006, // f32 (LUFS)
    OMEGA_PARAM_ANTI_DOLBY          = 0x00010007, // f32 [0..1]
    OMEGA_PARAM_HIGH_CUT_HZ         = 0x00010008, // f32 (Hz)
    OMEGA_PARAM_BASS_BOOST_DB       = 0x00010009, // f32 (dB)
    OMEGA_PARAM_DIALOG_BOOST_DB     = 0x0001000A, // f32 (dB)
    OMEGA_PARAM_WIDENER_MULT        = 0x0001000B, // f32 [0..2]
    OMEGA_PARAM_ROUTE_MODE          = 0x0001000C, // i32 (RouteMode)
    OMEGA_PARAM_MASTER_BYPASS       = 0x0001000D, // i32 (0/1)
};

// Parseo AOSP effect_param_t: | u32 psize | u32 vsize | u8 param[psize] | pad4 | u8 value[vsize] |
// Sin structs nuevos, sin depender de headers que no tengamos.
static inline bool omega_param_read_id(const void* pCmdData, uint32_t cmdSize,
                                       uint32_t& outId, uint32_t& outVsize,
                                       const uint8_t*& outValPtr) noexcept {
    if (!pCmdData) return false;
    if (cmdSize < (uint32_t)(sizeof(uint32_t) * 3)) return false;
    const uint8_t* p = static_cast<const uint8_t*>(pCmdData);
    uint32_t psize, vsize;
    memcpy(&psize, p + 0,             sizeof(uint32_t));
    memcpy(&vsize, p + sizeof(uint32_t), sizeof(uint32_t));
    if (psize != sizeof(uint32_t) || vsize == 0) return false;
    const size_t hdr     = sizeof(uint32_t) * 2 + psize;
    const size_t val_off = (hdr + 3u) & ~size_t(3u);
    if (val_off + vsize > cmdSize) return false;
    memcpy(&outId, p + sizeof(uint32_t) * 2, sizeof(uint32_t));
    outVsize  = vsize;
    outValPtr = p + val_off;
    return true;
}

static inline bool omega_param_val_f32(const uint8_t* v, uint32_t vsize, float& out) noexcept {
    if (vsize != sizeof(float)) return false;
    memcpy(&out, v, sizeof(float));
    return std::isfinite(out);
}

static inline bool omega_param_val_i32(const uint8_t* v, uint32_t vsize, int32_t& out) noexcept {
    if (vsize != sizeof(int32_t)) return false;
    memcpy(&out, v, sizeof(int32_t));
    return true;
}

// EXPURGO (auditoría 2026-08-12): eliminados los 3 JNI fantasma
//   nativeInitDSP / nativeSetSpatialWidthDirect / nativeSetHarmonicGain
// y su singleton g_fusionCore.
//
// Por qué eran dead code:
//   Este archivo compila a libomega_effect.so (módulo AudioFlinger cargado
//   por audioserver vía dlopen, SIN máquina virtual ART). Los símbolos
//   Java_com_ivanna_omega_core_IvannaNativeLib_* nunca pueden resolverse
//   aquí — la app los llama contra libivanna_omega.so, donde YA existen
//   las definiciones reales:
//     - nativeInitDSP               → jni/ivanna_omega_jni.cpp:867
//     - nativeSetSpatialWidthDirect → jni/ivanna_omega_jni.cpp:1171
//     - nativeSetHarmonicGain       → jni/ivanna_omega_jni.cpp:1139
//   El pipeline real usa IvannaFusionCore PER-INSTANCE (ctx->fusionCore,
//   líneas 108/129/167/251/388) — nunca el global que estos JNI mantenían.
//   Efecto: -31 líneas, tabla de símbolos de libomega_effect.so limpia.
//   No se toca <jni.h> (otros símbolos del archivo lo pueden requerir).


/* ════════════════════════════════════════════════════════════════════════════
 *  IMPLEMENTACIÓN EFECTO AUDIOFLINGER (GlobalEffect / Magisk soundfx)
 *  Punto de entrada obligatorio: símbolo "AELI"
 *  (AUDIO_EFFECT_LIBRARY_INFO_SYM se expande a AELI vía audio_effect_compat.h)
 * ════════════════════════════════════════════════════════════════════════════ */

/* UUID propio del efecto IVANNA Omega */
static const effect_uuid_t OMEGA_EFFECT_UUID = {
    0x4956414e, 0x4e41, 0x4f4d, 0x4547, {0x41, 0x53, 0x55, 0x50, 0x52, 0x45}
};

static const effect_descriptor_t OMEGA_DESCRIPTOR = {
    {0, 0, 0, 0, {0, 0, 0, 0, 0, 0}},   /* type: null (propósito general) */
    {0x4956414e, 0x4e41, 0x4f4d, 0x4547, {0x41, 0x53, 0x55, 0x50, 0x52, 0x45}},
    EFFECT_CONTROL_API_VERSION,
    EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_ANY,
    100,                                  /* cpuLoad */
    1024,                                 /* memoryUsage (KB) */
    "IVANNA Omega Effect",
    "IVANNA OMEGA SUPREME"
};

/* Contexto por instancia. El PRIMER campo debe ser el puntero a la vtable.
 *
 * AUDIT FIX (session isolation): antes el estado DSP vivía en el global
 * g_fusionCore compartido entre TODAS las sesiones de AudioFlinger. Dos
 * pistas simultáneas (p.ej. música + navegación) pisaban el mismo estado
 * interno (spatial renderer, HRTF, smoothers) -> glitches, saltos de
 * ganancia y cross-talk entre sesiones. Se mueve el IvannaFusionCore a
 * este contexto para que cada instancia tenga el suyo aislado.
 *
 * AUDIT FIX (realtime allocation): buffers L/R deinterleaved preasignados
 * en el contexto (una vez, en SET_CONFIG) para evitar
 * `static thread_local std::vector<float>::resize()` dentro del callback
 * realtime de AudioFlinger. La rama de resize() de std::vector puede
 * llamar a malloc bajo el hilo de audio -> jitter, XRun y en casos
 * extremos deadlock si el allocator toca un mutex compartido.
 *
 * AUDIT FIX (control plane reconnect): campo lastAppliedGen guarda la
 * última generation del OmegaControlBus (SHM) aplicada por esta instancia.
 * Antes, omega_process ignoraba TOTALMENTE el snapshot publicado por el
 * daemon (command_server publica pero el efecto no consumía): la UI
 * movía sliders pero AudioFlinger corría con los defaults inmutables.
 * Ahora cada instancia lee readLatest() al principio del callback y
 * aplica los parámetros a su ctx->fusionCore, restaurando el puente
 * cross-process UI -> daemon -> audioserver.
 */
struct omega_effect_context_t {
    const struct effect_interface_s *itfe;
    effect_config_t config;
    bool enabled;
    IvannaFusionEngine* fusionCore; // AUDIT FIX: DSP per-instance (era global)
                                    // AUDIT FIX (type mismatch): la clase base
                                    // IvannaFusionCore solo expone processBlock/
                                    // setParameter; los metodos que usa este
                                    // archivo (initSpatial/processStereo/
                                    // setSpatialWidth/...) viven en la derivada
                                    // IvannaFusionEngine. La instancia ya se crea
                                    // como new IvannaFusionEngine(sr) — el tipo
                                    // del puntero debe coincidir.
    // ── RIR sala — instanciados per-session, cargados lazy al primer SET_ROOM_RT60
    Ivanna::RirConvolver* rirConvolver; // convolucionador overlap-save (null hasta 1er load)
    Ivanna::RirDataset*   rirDataset;   // dataset de 200 salas (cargado una vez por proceso)
    float* rtL;                     // AUDIT FIX: buffer L preasignado (realtime)
    float* rtR;                     // AUDIT FIX: buffer R preasignado (realtime)
    int    rtCapacity;              // frames que caben en rtL/rtR
    uint64_t lastAppliedGen;        // AUDIT FIX: seguimiento de la generation SHM
    bool     ctrlBusOpen;           // AUDIT FIX: OmegaControlBus reader ready
    bool     chunkedWarned;         // AUDIT FIX: log único cuando se procesa en chunks
    // FIX (distorsion digital): limiter por instancia al final de la cadena
    // (tras expansion M/S TinyML + RIR). calloc zero-init deja el puntero
    // en nullptr; se instancia lazy en SET_CONFIG junto a los buffers RT.
    ivanna::SafetyLimiter* safetyLimiter;
    // AUDIT FIX #4 (plano de control): estado del writer local para
    // dispositivos sin daemon. Solo se abre si el reader del daemon falló.
    // Snapshot que se publica al recibir SET_PARAM: se conserva entre
    // llamadas para no perder los valores ya recibidos.
    bool                     localWriterOpen;
    ivanna::OmegaDspSnapshot pendingSnap;
};

// AUDIT FIX #4: writer local por instancia. El SHM del daemon vive en
// /data/adb/ivanna_omega/... — path root-only. El writer local usa una
// región propia por-proceso (audioserver), NUNCA la del daemon: dos writers
// concurrentes romperían el seqlock. Solo se abre si openReader() falló.
// Path unificado en include/omega_control_bus.h (OMEGA_EFFECT_LOCAL_BUS_PATH).
static constexpr const char* OMEGA_LOCAL_BUS_PATH =
    ivanna::OMEGA_EFFECT_LOCAL_BUS_PATH;

// Publica ctx->pendingSnap al bus local (writer local del efecto), o aplica
// directo si el writer aún no está abierto. Idempotente: si el reader
// (daemon) está activo y publica su propio snapshot después, ambos
// terminan aplicando la misma familia de parámetros — el último gana, que
// es el mismo comportamiento que UI -> daemon -> SHM.
static void omega_local_publish_or_apply(omega_effect_context_t* ctx) noexcept;

// Capacidad máxima esperada por bloque. AudioFlinger normalmente pasa
// bloques entre 128 y 2048 frames; se reserva un margen holgado para no
// necesitar reasignar nunca en la ruta caliente.
static constexpr int OMEGA_RT_MAX_FRAMES = 8192;

// AUDIT FIX (control plane reconnect): aplica un OmegaDspSnapshot recibido
// vía SHM al IvannaFusionCore local. Se llama fuera del hot-path (una vez
// por generation nueva). No hace malloc, no bloquea, no logea en cada
// llamada. Los parámetros que el core actual no expone (EQ, dialónboost,
// SAF, etc.) se ignoran hasta que se añadan setters en fases posteriores
// — lo que importa aquí es que los sliders visibles de la UI (spatial
// width, harmonic gain, compresor) SI lleguen al audio real.
static inline void omega_apply_snapshot(IvannaFusionEngine* fc,
                                        const ivanna::OmegaDspSnapshot& s) noexcept {
    if (!fc) return;
    // El route arbiter marca quién aplica DSP. Si no somos SYSTEM_WIDE,
    // ni intensity ni el resto tocan; el efecto queda enabled pero pasa.
    if (static_cast<ivanna::RouteMode>(s.active_route) != ivanna::RouteMode::SYSTEM_WIDE) {
        return;
    }
    // Spatial width (slider UI "Ancho espacial")
    if (std::isfinite(s.spatial_width) && s.spatial_width > 0.f) {
        fc->setSpatialWidth(s.spatial_width);
    }
    // Harmonic gain (slider UI "Ganancia armónica")
    if (std::isfinite(s.harmonic_gain) && s.harmonic_gain >= 0.f) {
        fc->setHarmonicGain(s.harmonic_gain);
    }
    // Compresor (threshold / ratio)
    if (std::isfinite(s.compressor) && s.compressor < 0.f) {
        const float ratio = 1.0f + 7.0f * std::clamp(s.comp_amount, 0.f, 1.f);
        fc->setCompressorParams(s.compressor, ratio);
    }
    // ── Cable SAF → Ruta B — vector latente q[7] → motor HRTF ──────────────
    // FIX: ABI v2 añade saf_q[7] + saf_q_valid al snapshot. Cuando el daemon
    // publica un SET_SAF_STATE con el vector latente, saf_q_valid=1 y los
    // valores llegan hasta aquí. setSafLatentParams() propaga q[] a
    // HrtfManager::setSafLatentQ() que modula:
    //   q[0] → curvatura Riemanniana (personalización espacial del HRTF)
    //   q[1] → sesgo de azimut fino ±10°
    //   q[2..6] → morph de dataset (reservado)
    // El comentario anterior ("q[7] no cabe sin cambio de ABI") ya no aplica —
    // la ABI fue extendida (saf_q[7] en omega_control_bus.h, OMEGA_CTRL_SAF_Q).
    if (s.saf_q_valid == 1u) {
        fc->setSafLatentParams(s.saf_q);
    }
    // Aplicar saf_gain como modulación de harmonic_gain (compatibilidad con
    // clientes que solo envían escalares SAF sin vector q[7]).
    if (std::isfinite(s.saf_gain) && s.saf_gain > 0.f) {
        const float safGain  = std::clamp(s.saf_gain, 0.5f, 2.0f);
        const float safDelta = std::isfinite(s.saf_delta_energy)
                                 ? std::clamp(s.saf_delta_energy, -1.f, 1.f)
                                 : 0.f;
        const float base = (std::isfinite(s.harmonic_gain) && s.harmonic_gain >= 0.f)
                             ? s.harmonic_gain : 1.0f;
        const float mod  = safGain * (1.0f - 0.15f * safDelta);
        fc->setHarmonicGain(std::clamp(base * mod, 0.f, 2.0f));
    }
}

// ── RIR dataset: inicialización fuera del hilo RT ───────────────────────────
// FIX RT (auditoría 2026-08-25): este init (new + lectura de disco de 200
// WAV) se hacía dentro de omega_apply_room, invocada desde omega_process —
// el callback de audio del daemon. El primer frame con una sala activa
// pagaba el coste de disco + malloc en el hilo RT -> XRun/glitch en el
// arranque (mismo bug reparado en la ruta JNI por 12e3ea52, aquí seguía
// vivo en la ruta daemon). Ahora se inicializa en EFFECT_CMD_SET_CONFIG
// (hilo de control, no-RT) vía omega_rir_dataset_init(); el audio thread
// solo hace acquire-load del puntero ya publicado.
static std::atomic<Ivanna::RirDataset*> g_rirDataset{nullptr};

// Llamar SOLO desde omega_command(EFFECT_CMD_SET_CONFIG) — hilo no-RT.
// Idempotente: solo carga la primera vez que tiene éxito.
static void omega_rir_dataset_init() noexcept {
    if (g_rirDataset.load(std::memory_order_acquire) != nullptr) return;
    Ivanna::RirDataset* ds = new Ivanna::RirDataset();
    const char* base = "/data/adb/ivanna_omega/rir";
    if (!ds->load(base)) {
        LOGW("RirDataset: no se pudo cargar desde %s — sala desactivada", base);
        delete ds;
        return;
    }
    LOGI("RirDataset: %d salas cargadas desde %s", (int)ds->roomCount(), base);
    // release-store: publica el dataset completamente construido al hilo RT
    g_rirDataset.store(ds, std::memory_order_release);
}

// ── Worker de carga de IR (Ruta B) ─────────────────────────────────────────
// FIX RT (2026-08-27): loadImpulseResponse() lee un WAV de disco y aloca
// vectores — antes se llamaba dentro de omega_process (callback de audio)
// en cada cambio de sala -> I/O + alloc en el hot path = XRun/tronido.
// Ahora el hot path solo publica {ctx, roomIdx} y este hilo de control
// hace disco + FFT, entregando vía RirConvolver::load() (thread-safe con
// process() vía pending_ + crossfade). El mutex solo protege secciones
// de microsegundos (registro + entrega); el disco corre sin él.
static std::mutex              g_rirBMtx;
static std::condition_variable g_rirBCv;
static std::atomic<bool>       g_rirBStarted{false};
static std::vector<omega_effect_context_t*> g_rirBLive;   // ctx vivos
static omega_effect_context_t* g_rirBCtx = nullptr;       // petición pendiente
static int32_t                 g_rirBIdx = -1;

static bool omega_rir_ctx_alive_locked(omega_effect_context_t* ctx) {
    for (auto* c : g_rirBLive) if (c == ctx) return true;
    return false;
}

static void omega_rir_worker_loop() {
    std::unique_lock<std::mutex> lk(g_rirBMtx);
    while (true) {
        g_rirBCv.wait(lk, [] { return g_rirBIdx >= 0; });
        omega_effect_context_t* ctx = g_rirBCtx;
        const int32_t idx = g_rirBIdx;
        g_rirBIdx = -1; g_rirBCtx = nullptr;
        if (idx < 0 || !ctx) continue;
        Ivanna::RirDataset* ds = g_rirDataset.load(std::memory_order_acquire);
        if (!ds) continue;
        // Fase lenta SIN el mutex: disco + alloc (ds es proceso-global,
        // inmutable tras su carga única en SET_CONFIG).
        lk.unlock();
        std::vector<float> irL, irR;
        int sr = 0;
        const bool ok = ds->loadImpulseResponse((size_t)idx, irL, irR, sr)
                        && !irL.empty();
        lk.lock();
        // Fase rápida CON el mutex: entregar solo si el ctx sigue vivo
        // (release_effect des-registra bajo el mismo mutex -> sin UAF).
        if (!omega_rir_ctx_alive_locked(ctx) || !ctx->rirConvolver) continue;
        if (!ok) {
            LOGW("RirDataset: sala idx=%d no se pudo cargar — bypass", (int)idx);
            ctx->rirConvolver->setWetDry(0.f);
            continue;
        }
        int irLen = (int)irL.size();
        if (irLen > Ivanna::RirConvolver::MAX_IR) irLen = Ivanna::RirConvolver::MAX_IR;
        ctx->rirConvolver->load(irL.data(), irR.data(), irLen);
        LOGI("RirConvolver: sala idx=%d sr=%dHz cargada (worker, fuera de RT)",
             (int)idx, sr);
    }
}

// Llamable desde el hilo de audio: publicación breve bajo mutex (contención
// ~cero — el worker solo lo retiene para registro/entrega, nunca para disco).
static void omega_rir_post_load(omega_effect_context_t* ctx, size_t roomIdx) noexcept {
    std::lock_guard<std::mutex> lk(g_rirBMtx);
    g_rirBCtx = ctx;
    g_rirBIdx = (int32_t)roomIdx;
    g_rirBCv.notify_one();
}

// ── Cable RIR: aplica la sala seleccionada por SET_ROOM_RT60 ─────────────────
// Llamada desde omega_process cuando el snapshot tiene un room_rt60_s nuevo.
// Si rt60==0 → bypass. Si hay una sala cargada con ese RT60 → load() en el
// RirConvolver (lock-free, el proceso() del próximo bloque la absorbe).
static inline void omega_apply_room(omega_effect_context_t* ctx,
                                    const ivanna::OmegaDspSnapshot& s) noexcept {
    if (!ctx) return;

    // Lazy-init del dataset (cargado una sola vez por proceso audioserver)
    // FIX (log real CI 2026-08-13): la API real de Ivanna::RirDataset difiere
    // de lo que este archivo asumía — verificado contra spatial/RirDataset.hpp/.cpp
    // (la clase que YO construí y probé contra las 200 salas reales), no adivinado:
    //   - load(dir) toma UN argumento (deriva "<dir>/metadata.csv" internamente),
    //     no load(dir, csvPath).
    //   - roomCount(), no size().
    //   - findNearestByRT60(rt60) devuelve size_t (índice), no un puntero a una
    //     struct con .ir embebido — la IR se carga aparte vía loadImpulseResponse().
    // Audio thread (RT): solo acquire-load. Si el dataset aún no se publicó
    // (SET_CONFIG no corrió o la carga falló) -> bypass seco, SIN tocar disco
    // ni hacer malloc en este hilo.
    Ivanna::RirDataset* ds = g_rirDataset.load(std::memory_order_acquire);
    if (!ds || ds->roomCount() == 0 || !ctx->rirConvolver) {
        if (ctx->rirConvolver) {
            ctx->rirConvolver->setWetDry(0.f);
        }
        return;  // dry path: sin sala hasta que SET_CONFIG la prepare
    }

    const float rt60 = s.room_rt60_s;
    const float wet  = s.room_wet;

    if (rt60 < 0.01f) {
        // Bypass: desactivar convolver
        ctx->rirConvolver->setWetDry(0.f);
        ctx->rirConvolver->unload();
        return;
    }

    // Selección inteligente multi-criterio (TAREA 2): RT60 prioriza (60%),
    // volumen geométrico desempata (25%), distancia fuente→mic refina (15%).
    // Con solo RT60 había empates y saltos de sala arbitrarios entre salas
    // con reverberación casi idéntica pero geometría opuesta (pasillo
    // largo vs cubiculo compacto suenan distinto al mismo RT60).
    const size_t roomIdx = ds->findNearestSmart(rt60);

    // Solo recargar si la sala cambió (comparar por idx)
    const int32_t targetIdx = s.room_idx;
    if (targetIdx >= 0 && static_cast<size_t>(targetIdx) == roomIdx && ctx->rirConvolver->isLoaded()) {
        // Si el índice no cambió, solo actualizar wet/dry
        ctx->rirConvolver->setWetDry(wet);
        return;
    }

    // FIX RT (2026-08-27): la lectura del WAV + vectores salen de este hilo
    // (omega_process = callback de audio). El worker de control hace disco +
    // FFT y entrega vía RirConvolver::load() (crossfade incluido). El wet se
    // aplica ya: si el IR aún no llegó, process() es no-op hasta entonces —
    // la sala entra ~1-5 ms después, sin tronido ni XRun. La truncación a
    // MAX_IR la hace el worker (mismo criterio: reflejo temprano conservado).
    ctx->rirConvolver->setWetDry(wet);
    omega_rir_post_load(ctx, roomIdx);
}

/* ── Funciones de instancia (vtable) ─────────────────────────────────────── */
static int32_t omega_process(effect_handle_t self,
                             audio_buffer_t *inBuf, audio_buffer_t *outBuf) {
    omega_effect_context_t *ctx = reinterpret_cast<omega_effect_context_t *>(self);
    if (!ctx || !ctx->enabled || !inBuf || !outBuf) return 0;
    if (!inBuf->raw || !outBuf->raw || inBuf->frameCount == 0) return 0;

    const int frames = (int)inBuf->frameCount;
    const float* in = inBuf->f32;
    float* out = outBuf->f32;

    // AUDIT FIX (session isolation): usar el fusionCore de ESTA instancia,
    // nunca el global g_fusionCore. Motor aún no configurado: passthrough.
    IvannaFusionEngine* fc = ctx->fusionCore;
    if (!fc) {
        memmove(outBuf->raw, inBuf->raw, (size_t)frames * 2u * sizeof(float));
        return 0;
    }

    // AUDIT FIX (control plane reconnect): drena a lo más UN snapshot nuevo
    // por callback. readLatest() es lock-free (seqlock en SHM), no bloquea,
    // no malloc, y solo retorna true si hay una generation posterior a la
    // ya aplicada. Esto conecta finalmente los sliders de la UI -> daemon
    // -> SHM -> omega_process, cerrando el ciclo que estaba roto.
    if (ctx->ctrlBusOpen) {
        ivanna::OmegaDspSnapshot snap;
        if (ivanna::effectControlBus().readLatest(snap, ctx->lastAppliedGen)) {
            omega_apply_snapshot(fc, snap);
            omega_apply_room(ctx, snap);  // cable RIR: sala desde snapshot
        }
    }

    // AUDIT FIX (realtime allocation): buffers L/R preasignados en el ctx
    // (SET_CONFIG). Si vinieran sin reservar (calloc falló en SET_CONFIG)
    // se cae a passthrough — jamás asignar en el hilo de audio.
    if (!ctx->rtL || !ctx->rtR || ctx->rtCapacity <= 0) {
        memmove(outBuf->raw, inBuf->raw, (size_t)frames * 2u * sizeof(float));
        return 0;
    }
    float* L = ctx->rtL;
    float* R = ctx->rtR;

    // AUDIT FIX (rigid buffer / silent bypass): si el bloque entrante excede
    // la capacidad preasignada (p.ej. AudioFlinger con LDAC/LHDC puede lanzar
    // bloques grandes), ya NO se hace bypass silencioso a plano. Se procesa
    // el bloque completo en chunks de a lo sumo rtCapacity frames, reutilizando
    // los mismos buffers L/R — sin malloc, sin locks, mismo coste de memoria.
    // Solo se logea UNA vez por instancia (flag en el ctx, no en el hot path)
    // para no inundar logcat desde el callback de audio.
    if (frames > ctx->rtCapacity && !ctx->chunkedWarned) {
        ctx->chunkedWarned = true;
        LOGW("omega_process: frameCount=%d > rtCapacity=%d — procesando en chunks (sin bypass)",
             frames, ctx->rtCapacity);
    }

    int offset = 0;
    while (offset < frames) {
        const int chunk = ((frames - offset) < ctx->rtCapacity)
                          ? (frames - offset) : ctx->rtCapacity;
        const float* inChunk  = in  + (size_t)offset * 2u;
        float*       outChunk = out + (size_t)offset * 2u;

        for (int n = 0; n < chunk; ++n) {
            L[n] = inChunk[2 * n];
            R[n] = inChunk[2 * n + 1];
        }

        // Render binaural de objetos (VBAP + HRTF) + DSP de salida
        fc->processStereo(L, R, (size_t)chunk);

        // FASE 3: Integración de TinyML Asíncrono
        if (auto* classifier = fc->getClassifier()) {
            uint8_t domClass = classifier->getDominantClass();
            // 0: Speech, 1: Music, 2: Transient, 3: Noise
            if (domClass == 0) {
                // Speech: Focus vocal (reducción espacial sutil)
                for (int n = 0; n < chunk; ++n) {
                    float m = (L[n] + R[n]) * 0.5f;
                    float s = (L[n] - R[n]) * 0.5f;
                    s *= 0.8f; // Atenuar side
                    L[n] = m + s;
                    R[n] = m - s;
                }
            } else if (domClass == 1) {
                // Music: Expansión estéreo armónica
                for (int n = 0; n < chunk; ++n) {
                    float m = (L[n] + R[n]) * 0.5f;
                    float s = (L[n] - R[n]) * 0.5f;
                    s *= 1.2f; // Expandir side
                    L[n] = m + s;
                    R[n] = m - s;
                }
            }
        }

        // Cable RIR: aplicar reverberación de sala si está activa
        if (ctx->rirConvolver) ctx->rirConvolver->process(L, R, chunk);

        // FIX (distorsion digital): ultimo eslabon de la cadena — el mismo
        // SafetyLimiter que corre al final de la Ruta A. Sin esto, la
        // expansion mid/side del TinyML (s *= 1.2f en clase 'Music') o un
        // RIR con ganancia alta clippeaban directo al interleave.
        // process() es branchless NEON, sin malloc/locks: seguro en RT.
        if (ctx->safetyLimiter) ctx->safetyLimiter->process(L, R, chunk);

        // Interleave -> salida
        for (int n = 0; n < chunk; ++n) {
            outChunk[2 * n]     = L[n];
            outChunk[2 * n + 1] = R[n];
        }
        offset += chunk;
    }

    // ── Telemetría de audio real → OmegaControlBus local ─────────────────────
    // Calcula RMS y peak sobre la salida procesada y los escribe en el snapshot
    // pendiente (ctx->pendingSnap). OmegaControlBus.publish() los propaga via
    // SHM al proceso de la app, donde audioRouteBridgeLoop() los lee cada 30ms.
    // Sin esto: audioRouteBridgeLoop() nunca detecta Ruta B activa porque
    // omega_daemon_get_shared_state() usa SHM compartida; UI refleja audio cuando el daemon está conectado.
    {
        const float *proc = outBuf->f32;
        const uint32_t outFrames = (uint32_t)frames;
        float sumSq = 0.0f, pk = 0.0f;
        for (uint32_t i = 0; i < outFrames * 2u; ++i) {
            const float s = proc[i];
            sumSq += s * s;
            if (s > pk) pk = s; else if (-s > pk) pk = -s;
        }
        const float rms = (outFrames > 0)
            ? __builtin_sqrtf(sumSq / (float)(outFrames * 2u)) : 0.0f;
        ctx->pendingSnap.raw_rms   = rms;
        ctx->pendingSnap.raw_peak  = pk;
        ctx->pendingSnap.effect_frames += (uint64_t)outFrames;
        // publish() es no-op si el daemon no abrió el bus — seguro en ruta caliente
        if (ctx->localWriterOpen) {
            ivanna::effectControlBus().publish(ctx->pendingSnap);
        }
    }
    return 0;
}

static int32_t omega_command(effect_handle_t self, uint32_t cmdCode,
                             uint32_t cmdSize, void *pCmdData,
                             uint32_t *replySize, void *pReplyData) {
    omega_effect_context_t *ctx = reinterpret_cast<omega_effect_context_t *>(self);
    switch (cmdCode) {
        case EFFECT_CMD_INIT:
        case EFFECT_CMD_RESET:
            break;
        case EFFECT_CMD_SET_CONFIG:
            if (pCmdData && cmdSize == sizeof(effect_config_t)) {
                ctx->config = *reinterpret_cast<effect_config_t *>(pCmdData);
                uint32_t sr = ctx->config.outputCfg.samplingRate;
                if (sr == 0) sr = ctx->config.inputCfg.samplingRate;
                if (sr == 0) sr = 48000;
                // AUDIT FIX (session isolation): DSP se instancia POR CONTEXTO.
                // Cada sesión AudioFlinger llega aquí y crea su propio
                // IvannaFusionCore; ya no se pisa el global entre sesiones.
                if (!ctx->fusionCore) {
                    ctx->fusionCore = new IvannaFusionEngine((float)sr);
                }
                ctx->fusionCore->initSpatial((float)sr, 4096);
                // AUDIT FIX (realtime allocation): preasignar buffers L/R
                // AQUÍ (fuera de la ruta caliente) para que omega_process
                // no tenga que llamar a malloc/resize en el callback.
                if (!ctx->rtL) {
                    ctx->rtL = reinterpret_cast<float*>(
                        calloc((size_t)OMEGA_RT_MAX_FRAMES, sizeof(float)));
                }
                if (!ctx->rtR) {
                    ctx->rtR = reinterpret_cast<float*>(
                        calloc((size_t)OMEGA_RT_MAX_FRAMES, sizeof(float)));
                }
                ctx->rtCapacity =
                    (ctx->rtL && ctx->rtR) ? OMEGA_RT_MAX_FRAMES : 0;
                // Limiter por sesion: misma instancia que la Ruta A usa en
                // ivanna_omega_jni.cpp (g_safety_limiter) pero por-contexto,
                // asi dos sesiones simultaneas no comparten estado de gain-
                // reduction ni se contaminan la telemetria entre si.
                if (!ctx->safetyLimiter) ctx->safetyLimiter = new ivanna::SafetyLimiter();
                if (ctx->safetyLimiter) ctx->safetyLimiter->setParams();
                // FIX RT (2026-08-25): precargar dataset RIR (disco) y crear
                // el convolver AQUÍ, en el hilo de control — nunca en el
                // callback omega_process. Ver omega_rir_dataset_init().
                omega_rir_dataset_init();
                if (!ctx->rirConvolver) ctx->rirConvolver = new Ivanna::RirConvolver();
                // FIX RT (2026-08-27): arrancar el worker de carga de IR
                // (hilo de control, proceso-global). Idempotente; el hilo
                // duerme en la CV hasta que omega_apply_room publique una
                // sala pendiente — cero coste mientras no hay cambios.
                if (!g_rirBStarted.exchange(true, std::memory_order_acq_rel)) {
                    std::thread(omega_rir_worker_loop).detach();
                    // detach a propósito: vive el ciclo de vida del proceso
                    // audioserver; el acceso a ctx se protege con g_rirBLive
                    // bajo g_rirBMtx (release_effect des-registra antes de
                    // liberar -> sin UAF aunque el hilo sobreviva al efecto).
                }
                // AUDIT FIX (control plane reconnect): abrir el reader del
                // OmegaControlBus (SHM cross-process). Si el daemon todavía
                // no creó el SHM (arranque en frío del audioserver), la
                // apertura simplemente falla y el efecto sigue como
                // passthrough respecto al control plane; en el próximo
                // SET_CONFIG (cada apertura de sesión) se reintenta.
                if (!ctx->ctrlBusOpen) {
                    ctx->ctrlBusOpen = ivanna::effectControlBus().openReader();
                    if (ctx->ctrlBusOpen) {
                        // Sembrar el estado inicial con el snapshot más reciente
                        // ya publicado por el daemon — sin esto, el primer
                        // bloque de audio corre con los defaults del core hasta
                        // que llegue el siguiente publish() de la UI.
                        ivanna::OmegaDspSnapshot seed;
                        uint64_t seen = 0;
                        if (ivanna::effectControlBus().readLatest(seed, seen)) {
                            omega_apply_snapshot(ctx->fusionCore, seed);
                            ctx->lastAppliedGen = seen;
                            LOGI("OmegaControlBus attached (seed gen=%llu route=%d)",
                                 (unsigned long long)seen,
                                 (int)seed.active_route);
                        } else {
                            LOGI("OmegaControlBus attached (no snapshot yet)");
                        }
                    }
                }
                // Dataset HRTF personalizado (si existe). Fallback: sintético.
                // El resultado se logea SIEMPRE: es el unico punto del sistema
                // donde se decide entre HRTF medido y HRTF sintetico, y esa
                // decision cambia por completo la calidad de la espacializacion.
                static const char* kHrtfPath =
                    "/data/adb/ivanna_omega/hrtf_dataset.ihr1";
                errno = 0;
                if (ctx->fusionCore->loadCustomHrtf(kHrtfPath)) {
                    LOGI("Custom HRTF dataset loaded from %s (measured path ACTIVE)",
                         kHrtfPath);
                } else {
                    // errno solo es significativo si el fallo vino del open();
                    // si el archivo existe pero la cabecera IHR1 es invalida,
                    // errno queda en 0 y hay que decirlo en vez de imprimir
                    // "Success", que seria peor que no logear nada.
                    const int err = errno;
                    LOGW("Failed to load custom HRTF dataset from %s (errno=%d, %s). "
                         "Falling back to SYNTHETIC HRTF.",
                         kHrtfPath, err,
                         err != 0 ? strerror(err)
                                  : "file readable but not a valid IHR1 dataset");
                }
            }
            break;
        case EFFECT_CMD_ENABLE:
            ctx->enabled = true;
            break;
        case EFFECT_CMD_DISABLE:
            ctx->enabled = false;
            break;
        case EFFECT_CMD_SET_PARAM:
        case EFFECT_CMD_SET_PARAM_COMMIT: {
            // AUDIT FIX #4: enrutar al UnifiedControlBus existente (SHM).
            // Camino cerrado: parseo AOSP -> pendingSnap -> aplicar directo
            // a ctx->fusionCore (nunca se pierde el parámetro) -> publish
            // al bus local (o no-op si daemon activo, ya aplicó). Sin IPC
            // nuevo, sin degradar el path del daemon.
            if (!ctx) break;
            uint32_t id = 0, vsize = 0;
            const uint8_t* val = nullptr;
            if (!omega_param_read_id(pCmdData, cmdSize, id, vsize, val)) {
                LOGW("SET_PARAM: layout inválido (cmdSize=%u)", cmdSize);
                break;
            }
            ivanna::OmegaDspSnapshot& s = ctx->pendingSnap;
            // Semilla del snapshot en la primera invocación: preserva
            // magic/version/route por defecto (SYSTEM_WIDE) para que
            // omega_apply_snapshot NO lo rechace como "otra ruta".
            if (s.magic != ivanna::OMEGA_CTRL_MAGIC) {
                s = ivanna::OmegaDspSnapshot::makeDefault();
            }
            bool touched = false;
            switch (id) {
                case OMEGA_PARAM_INTENSITY: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.intensity = std::clamp(v, 0.f, 1.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_SPATIAL_WIDTH: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.spatial_width = std::clamp(v, 0.f, 3.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_HARMONIC_GAIN: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.harmonic_gain = std::clamp(v, 0.f, 2.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_COMPRESSOR_THRESHOLD: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        // dB negativos; clamp a rango razonable.
                        s.compressor = std::clamp(v, -60.f, 0.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_COMPRESSOR_AMOUNT: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.comp_amount = std::clamp(v, 0.f, 1.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_LOUDNESS_TARGET: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.loudness_target = std::clamp(v, -36.f, -6.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_ANTI_DOLBY: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.anti_dolby = std::clamp(v, 0.f, 1.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_HIGH_CUT_HZ: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.high_cut_hz = std::clamp(v, 0.f, 24000.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_BASS_BOOST_DB: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.bass_boost_db = std::clamp(v, -12.f, 12.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_DIALOG_BOOST_DB: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.dialog_boost_db = std::clamp(v, -12.f, 12.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_WIDENER_MULT: {
                    float v; if (omega_param_val_f32(val, vsize, v)) {
                        s.widener_mult = std::clamp(v, 0.f, 2.f); touched = true;
                    }
                } break;
                case OMEGA_PARAM_ROUTE_MODE: {
                    int32_t v; if (omega_param_val_i32(val, vsize, v)) {
                        if (v < 0) v = 0; if (v > 2) v = 2;
                        s.active_route = v; touched = true;
                    }
                } break;
                case OMEGA_PARAM_MASTER_BYPASS: {
                    int32_t v; if (omega_param_val_i32(val, vsize, v)) {
                        // bit 0 del campo flags: bypass global.
                        if (v) s.flags |= 0x1u;
                        else   s.flags &= ~0x1u;
                        touched = true;
                    }
                } break;
                default:
                    // ID desconocido → no-op explícito (no toca el snapshot,
                    // no envenena el bus). Antes esto ni se logueaba.
                    LOGW("SET_PARAM ignorado: id=0x%08x vsize=%u", id, vsize);
                    break;
            }
            if (touched) {
                omega_local_publish_or_apply(ctx);
            }
        } break;
        case EFFECT_CMD_GET_PARAM:
        case EFFECT_CMD_SET_DEVICE:
        case EFFECT_CMD_SET_VOLUME:
        case EFFECT_CMD_SET_AUDIO_MODE:
            break;
        default:
            break;
    }
    if (replySize && pReplyData && *replySize >= sizeof(int32_t))
        *reinterpret_cast<int32_t *>(pReplyData) = 0;
    return 0;
}

static int32_t omega_get_descriptor(effect_handle_t self,
                                    effect_descriptor_t *pDescriptor) {
    if (pDescriptor) *pDescriptor = OMEGA_DESCRIPTOR;
    return 0;
}

static int32_t omega_process_reverse(effect_handle_t self,
                                     audio_buffer_t *inBuf, audio_buffer_t *outBuf) {
    return 0;
}

static const struct effect_interface_s OMEGA_INTERFACE = {
    omega_process,
    omega_command,
    omega_get_descriptor,
    omega_process_reverse
};

/* ── Funciones de librería (dlopen entry points) ─────────────────────────── */
static int32_t omega_query_num_effects(uint32_t *pNumEffects) {
    if (pNumEffects) *pNumEffects = 1;
    return 0;
}

static int32_t omega_query_effect(uint32_t index, effect_descriptor_t *pDescriptor) {
    if (index != 0 || !pDescriptor) return -EINVAL;
    *pDescriptor = OMEGA_DESCRIPTOR;
    return 0;
}

static int32_t omega_create_effect(const effect_uuid_t *uuid, int32_t sessionId,
                                   int32_t ioId, effect_handle_t *pHandle) {
    if (!pHandle) return -EINVAL;
    omega_effect_context_t *ctx =
        reinterpret_cast<omega_effect_context_t *>(calloc(1, sizeof(omega_effect_context_t)));
    if (!ctx) return -ENOMEM;
    ctx->itfe = &OMEGA_INTERFACE;
    ctx->enabled = false;
    ctx->fusionCore = nullptr;   // AUDIT FIX: init explícito (per-instance DSP)
    ctx->rirConvolver = nullptr;
    ctx->rtL = nullptr;          // AUDIT FIX: buffers RT se reservan en SET_CONFIG
    ctx->rtR = nullptr;
    ctx->rtCapacity = 0;
    ctx->lastAppliedGen = 0;     // AUDIT FIX: control plane empieza sin generation
    ctx->ctrlBusOpen    = false;
    ctx->localWriterOpen = false;  // AUDIT FIX #4: writer local se abre lazy
    // pendingSnap queda con magic=0 hasta el primer SET_PARAM (se sembrará
    // con makeDefault() ahí).
    memset(&ctx->pendingSnap, 0, sizeof(ctx->pendingSnap));
    {
        // Registrar en el set de ctx vivos del worker RIR (Ruta B) — el
        // worker solo entrega IR a ctx registrados (anti-UAF).
        std::lock_guard<std::mutex> lk(g_rirBMtx);
        g_rirBLive.push_back(ctx);
    }
    *pHandle = reinterpret_cast<effect_handle_t>(ctx);
    return 0;
}

// AUDIT FIX #4: implementación del writer local + apply directo. Se pone
// aquí (post-declaración de ctx) para no reordenar los símbolos AELI.
static void omega_local_publish_or_apply(omega_effect_context_t* ctx) noexcept {
    if (!ctx) return;
    // (a) Apply directo al DSP local — nunca se pierde el parámetro.
    if (ctx->fusionCore) {
        omega_apply_snapshot(ctx->fusionCore, ctx->pendingSnap);
    }
    // (b) Publicar al bus SHM para que otras instancias del efecto en el
    //     mismo proceso audioserver (múltiples sesiones AudioFlinger) vean
    //     el cambio en el próximo readLatest(). Solo abrimos writer local
    //     si el reader del daemon NO está activo — dos writers sobre el
    //     mismo SHM romperían el seqlock.
    if (ctx->ctrlBusOpen) {
        // Daemon activo → él es el writer autoritativo. El apply directo
        // de (a) es suficiente hasta el siguiente publish() del daemon.
        return;
    }
    if (!ctx->localWriterOpen) {
        ctx->localWriterOpen =
            ivanna::effectControlBus().openWriter(OMEGA_LOCAL_BUS_PATH);
        if (ctx->localWriterOpen) {
            LOGI("OmegaControlBus local writer opened at %s", OMEGA_LOCAL_BUS_PATH);
        }
    }
    if (ctx->localWriterOpen) {
        ivanna::effectControlBus().publish(ctx->pendingSnap);
    }
}

static int32_t omega_release_effect(effect_handle_t handle) {
    // AUDIT FIX (lifecycle leak): antes sólo se hacía free(handle), dejando
    // fugado el IvannaFusionCore alojado en ctx->fusionCore. En sesiones
    // largas de AudioFlinger (crear/destruir efectos por cada foco de
    // audio) esto acumulaba megas de estado DSP (buffers HRTF, spatial
    // renderer, smoothers) hasta OOM del audioserver. Se libera
    // explícitamente el DSP antes de liberar el contexto.
    if (handle) {
        omega_effect_context_t *ctx =
            reinterpret_cast<omega_effect_context_t *>(handle);
        {
            // FIX UAF (2026-08-27): des-registrar del set de ctx vivos del
            // worker RIR ANTES de liberar nada — si el hilo estaba a mitad
            // de una entrega (disco ya leído, esperando el mutex), al
            // adquirirlo comprobará alive() == false y descartará la IR
            // en vez de escribir sobre memoria liberada.
            std::lock_guard<std::mutex> lk(g_rirBMtx);
            for (size_t i = 0; i < g_rirBLive.size(); ++i) {
                if (g_rirBLive[i] == ctx) {
                    g_rirBLive[i] = g_rirBLive.back();
                    g_rirBLive.pop_back();
                    break;
                }
            }
            if (g_rirBCtx == ctx) { g_rirBCtx = nullptr; g_rirBIdx = -1; }
        }
        if (ctx->fusionCore) {
            delete ctx->fusionCore;
            ctx->fusionCore = nullptr;
        }
        // FIX lifecycle: el limiter se liberaba DENTRO del if(fusionCore)
        // — si fusionCore era null (init fallido) pero safetyLimiter no,
        // quedaba fugado y su puntero nunca se anulaba. Liberacion
        // independiente y nulificado de ambos.
        if (ctx->safetyLimiter) {
            delete ctx->safetyLimiter;
            ctx->safetyLimiter = nullptr;
        }
        if (ctx->rirConvolver) {
            delete ctx->rirConvolver;
            ctx->rirConvolver = nullptr;
        }
        // AUDIT FIX (realtime allocation): liberar buffers RT preasignados.
        if (ctx->rtL) { free(ctx->rtL); ctx->rtL = nullptr; }
        if (ctx->rtR) { free(ctx->rtR); ctx->rtR = nullptr; }
        ctx->rtCapacity = 0;
        free(ctx);
    }
    return 0;
}

static int32_t omega_get_descriptor_lib(const effect_uuid_t *uuid,
                                        effect_descriptor_t *pDescriptor) {
    if (!pDescriptor) return -EINVAL;
    *pDescriptor = OMEGA_DESCRIPTOR;
    return 0;
}

// ── Cable SAF → FusionCore ────────────────────────────────────────────────────
// EXPURGO (auditoría 2026-08-12, continuación del expurgo JNI):
// ivanna_saf_apply_latent() también era fantasma en este target.
//   * La definición real la provee saf_latent_bridge.cpp:35 en
//     libivanna_omega.so (proceso app), que publica q[7] en un
//     snapshot atómico con seqlock-lite (ivanna_saf_get_latent_snapshot).
//   * Esta copia vivía en libomega_effect.so (proceso audioserver,
//     sin ART) — nunca fue enlazada por SaFJniBridge (ese TU compila
//     al target app, no a éste).
//   * Además dependía del singleton g_fusionCore que el expurgo JNI
//     anterior eliminó → quedó rota por construcción.
// El push SAF→ObjectRenderer inter-proceso sigue siendo alcance
// separado (documentado en saf_latent_bridge.cpp).

/* ── SÍMBOLO "AELI" — el que audioserver busca con dlsym() ───────────────── */
extern "C" __attribute__((visibility("default"), used))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    AUDIO_EFFECT_LIBRARY_TAG,
    EFFECT_LIBRARY_API_VERSION,
    "IVANNA Omega Effect Library",
    "IVANNA OMEGA SUPREME",
    omega_query_num_effects,
    omega_query_effect,
    omega_create_effect,
    omega_release_effect,
    omega_get_descriptor_lib
};
