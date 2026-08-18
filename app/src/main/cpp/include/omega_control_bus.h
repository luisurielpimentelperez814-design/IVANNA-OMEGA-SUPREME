#pragma once
/*
 * ============================================================
 * IVANNA OMEGA SUPREME — OmegaControlBus
 *
 * Control Plane cross-process para audio system-wide.
 *
 * ARQUITECTURA:
 *   Writer (daemon process):
 *     - OmegaControlBus::openWriter(path) → mmap con MAP_SHARED+PROT_WRITE
 *     - OmegaControlBus::publish(snapshot) → seqlock atómico en SHM
 *     - command_server.cpp es el único publicador autorizado
 *
 *   Reader (audioserver / omega_effect.so):
 *     - OmegaControlBus::openReader(path) → mmap con MAP_SHARED+PROT_READ
 *     - OmegaControlBus::readLatest(out)  → seqlock read, no bloquea
 *     - omega_effect.cpp consume y aplica si route == SYSTEM_WIDE
 *
 *   Route Arbiter:
 *     - RouteMode::SYSTEM_WIDE  → omega_effect aplica, nativeProcess pasa
 *     - RouteMode::IN_PROCESS   → nativeProcess aplica, omega_effect pasa
 *     - RouteMode::OFF          → nadie aplica DSP
 *
 * GARANTÍAS:
 *   - OmegaDspSnapshot es trivialmente copiable (POD puro, sin punteros)
 *   - Seqlock embebido: guard es odd durante escritura, even en reposo
 *   - Validable por MAGIC + VERSION + CRC32
 *   - Tamaño <= 512 bytes (verificado con static_assert)
 *   - Sin malloc, sin std::vector, sin std::string, sin heap
 *   - Seguro para placement-new sobre región mmap
 *   - Sin locks del SO en el hot path de lectura
 *
 * PROHIBIDO en omega_process() / audio callback:
 *   - malloc / new
 *   - locks pesados
 *   - parsing JSON
 *   - I/O bloqueante
 *   - logging excesivo
 *
 * SHM PATH: /data/adb/ivanna_omega/omega_control_snapshot
 * ============================================================
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Evitar conflictos con android/log.h en omega_effect.cpp
#ifndef OMEGA_CTRL_LOGD
#  if defined(__ANDROID__)
#    include <android/log.h>
#    define OMEGA_CTRL_LOGD(fmt,...) __android_log_print(ANDROID_LOG_DEBUG,"OmegaCtrlBus",fmt,##__VA_ARGS__)
#    define OMEGA_CTRL_LOGW(fmt,...) __android_log_print(ANDROID_LOG_WARN, "OmegaCtrlBus",fmt,##__VA_ARGS__)
#  else
#    include <cstdio>
#    define OMEGA_CTRL_LOGD(fmt,...) std::fprintf(stderr,"[OCB DBG] " fmt "\n",##__VA_ARGS__)
#    define OMEGA_CTRL_LOGW(fmt,...) std::fprintf(stderr,"[OCB WRN] " fmt "\n",##__VA_ARGS__)
#  endif
#endif

namespace ivanna {

// ══════════════════════════════════════════════════════════════════════════════
// RouteMode — Route Arbiter explícito
// ══════════════════════════════════════════════════════════════════════════════

enum class RouteMode : int32_t {
    OFF          = 0,  // Nadie aplica DSP
    IN_PROCESS   = 1,  // nativeProcess / Ruta A (en proceso de la app)
    SYSTEM_WIDE  = 2,  // omega_effect.so / Ruta B (audioserver, cross-process)
};

inline const char* routeModeStr(RouteMode r) noexcept {
    switch (r) {
        case RouteMode::OFF:         return "OFF";
        case RouteMode::IN_PROCESS:  return "IN_PROCESS";
        case RouteMode::SYSTEM_WIDE: return "SYSTEM_WIDE";
        default:                      return "UNKNOWN";
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// OmegaDspSnapshot — Snapshot de control (ABI fijo, versionado, cross-process)
//
// REGLAS ABI:
//   1. Solo campos trivialmente copiables (floats, ints, arrays fijos)
//   2. Sin punteros, sin VTables, sin herencia
//   3. MAGIC y VERSION deben coincidir para aceptar un snapshot
//   4. CRC32 cubre todos los campos desde generation hasta crc32 (exclusive)
//   5. Incrementar VERSION si cambia el layout
// ══════════════════════════════════════════════════════════════════════════════

constexpr uint32_t OMEGA_CTRL_MAGIC   = 0x4F4D4543u; // "OMEC"
constexpr uint16_t OMEGA_CTRL_VERSION = 1u;
constexpr int      OMEGA_CTRL_EQ_BANDS = 10;

// CRC32 simple (tabla inline, no requiere zlib)
inline uint32_t omega_crc32(const void* data, size_t len) noexcept {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

struct OmegaDspSnapshot {
    // ── Identidad ────────────────────────────────────────────────────────────
    uint32_t magic;            // OMEGA_CTRL_MAGIC — valida que es nuestro struct
    uint16_t version;          // OMEGA_CTRL_VERSION — valida compatibilidad ABI
    uint16_t _pad0;            // alineación

    // ── Control de generación ─────────────────────────────────────────────────
    uint64_t generation;       // Monotónicamente creciente; 0 = nunca publicado
    uint64_t timestamp_ms;     // CLOCK_MONOTONIC en ms al momento de publicar

    // ── Route Arbiter ─────────────────────────────────────────────────────────
    int32_t  active_route;     // RouteMode cast a int32_t
    uint32_t _pad1;            // alineación
    uint64_t consumer_generation; // Última generation que omega_effect confirmó aplicar

    // ── Intensidad global ─────────────────────────────────────────────────────
    float    intensity;        // [0, 1]

    // ── EQ ISO 226 ────────────────────────────────────────────────────────────
    float    eq_gains[OMEGA_CTRL_EQ_BANDS]; // dB por banda
    float    listen_phon;
    float    ref_phon;

    // ── Estado perceptual ─────────────────────────────────────────────────────
    float    compressor;
    float    exciter_reduction;
    float    high_cut_hz;
    float    spatial_width;
    float    loudness_target;
    float    harmonic_gain;
    float    anti_dolby;

    // ── Estado adaptativo ─────────────────────────────────────────────────────
    float    target_gain;
    float    comp_amount;
    float    exc_red;

    // ── PF Engine (13 parámetros) ─────────────────────────────────────────────
    float    pf_params[13];

    // ── Route profile ─────────────────────────────────────────────────────────
    float    bass_boost_db;
    float    dialog_boost_db;
    float    widener_mult;

    // ── SAF ───────────────────────────────────────────────────────────────────
    float    saf_delta_energy;
    float    saf_metric_norm;
    float    saf_memory;
    float    saf_gain;

    // ── Sala RIR ──────────────────────────────────────────────────────────────
    // Selección y mezcla de la respuesta al impulso de sala (RirDataset).
    // room_rt60_s: RT60 objetivo en segundos [0.1, 6.0] — selector de sala.
    //   0.0 = sala desactivada (bypass del convolver RIR).
    // room_idx: índice de la sala seleccionada en el dataset (0..199).
    //   Calculado por RirDataset::findByRt60(room_rt60_s).
    // room_wet: mezcla wet/dry [0.0, 1.0].
    //   0.0 = solo señal seca, 1.0 = solo señal con reverberación.
    float    room_rt60_s;   // RT60 objetivo [0.1,6.0], 0 = bypass
    int32_t  room_idx;      // índice sala [-1 = ninguna, 0..199]
    float    room_wet;      // wet/dry [0,1]

    // ── Flags ────────────────────────────────────────────────────────────────
    // bit 0: bypass global
    // bit 1: eq_calibrated
    // bit 2: perceptual_on
    // bit 3: anti_dolby_on
    uint32_t flags;

    // ── Telemetría de audio real (Ruta B) ────────────────────────────────────
    // Escritos por omega_effect.cpp en el proceso audioserver (cada bloque DSP).
    // Leídos por audioRouteBridgeLoop() en el proceso de la app vía OmegaControlBus.
    // Sin estos campos, audioRouteBridgeLoop() nunca detecta Ruta B activa aunque
    // sí esté procesando — por eso la UI marcaba "sin audio" con Ruta B viva.
    float    raw_rms;          // RMS del bloque actual [0, 1] — 0 = silencio
    float    raw_peak;         // Peak del bloque actual [0, 1]
    uint64_t effect_frames;    // Total de frames procesados por omega_effect (monotónico)

    // ── Integridad ────────────────────────────────────────────────────────────
    uint32_t crc32;            // CRC32 de bytes [magic .. effect_frames] inclusive

    // ── Helpers ───────────────────────────────────────────────────────────────

    static OmegaDspSnapshot makeDefault() noexcept {
        OmegaDspSnapshot s{};
        s.magic      = OMEGA_CTRL_MAGIC;
        s.version    = OMEGA_CTRL_VERSION;
        s.generation = 0;
        s.active_route = static_cast<int32_t>(RouteMode::SYSTEM_WIDE);
        s.intensity  = 0.85f;
        s.listen_phon = 40.f;
        s.room_rt60_s = 0.f;   // sala desactivada por defecto
        s.room_idx    = -1;
        s.room_wet    = 0.35f;
        s.ref_phon    = 70.f;
        s.loudness_target = -18.f;
        s.harmonic_gain   = 1.f;
        s.widener_mult    = 1.f;
        s.saf_gain        = 1.f;
        s.consumer_generation = 0;
        s.flags = 0;
        s.raw_rms      = 0.0f;
        s.raw_peak     = 0.0f;
        s.effect_frames = 0ULL;
        s.stampCrc();
        return s;
    }

    // Calcula y escribe el CRC sobre todos los campos excepto crc32 mismo
    void stampCrc() noexcept {
        crc32 = omega_crc32(this, offsetof(OmegaDspSnapshot, crc32));
    }

    bool isMagicValid()   const noexcept { return magic == OMEGA_CTRL_MAGIC; }
    bool isVersionValid() const noexcept { return version == OMEGA_CTRL_VERSION; }
    bool isCrcValid()     const noexcept {
        return crc32 == omega_crc32(this, offsetof(OmegaDspSnapshot, crc32));
    }
    bool isValid() const noexcept {
        return isMagicValid() && isVersionValid() && isCrcValid();
    }

    RouteMode route() const noexcept {
        return static_cast<RouteMode>(active_route);
    }
};

// Validar tamaño en tiempo de compilación (host + ARM64)
static_assert(sizeof(OmegaDspSnapshot) <= 512,
    "OmegaDspSnapshot supera 512 bytes — revisar campos o padding");
static_assert(std::is_trivially_copyable<OmegaDspSnapshot>::value,
    "OmegaDspSnapshot debe ser trivialmente copiable (POD) para seqlock + mmap");


// ══════════════════════════════════════════════════════════════════════════════
// SharedControlRegion — layout real de la región mmap
//
// El seqlock embebe guard ANTES del snapshot para que un lector que solo
// mapea la primera página pueda validar la consistencia sin leer el resto.
// guard es odd durante escritura, even en reposo.
// ══════════════════════════════════════════════════════════════════════════════

struct alignas(64) SharedControlRegion {
    std::atomic<uint32_t> guard;   // seqlock (odd = escritura en curso)
    uint32_t              _pad;    // alineación 8 bytes para snapshot
    OmegaDspSnapshot      snapshot;
};

static_assert(sizeof(SharedControlRegion) <= 4096,
    "SharedControlRegion no cabe en una página (4096 bytes)");


// ══════════════════════════════════════════════════════════════════════════════
// OmegaControlBus — Bus de control cross-process
//
// Writer (daemon):  openWriter() → publish()
// Reader (effect):  openReader() → readLatest()
//
// readLatest() es lock-free, no bloquea, seguro en audio callback.
// publish()    es wait-free (seqlock sin contención en writer único).
// ══════════════════════════════════════════════════════════════════════════════

class OmegaControlBus {
public:
    static constexpr const char* DEFAULT_PATH =
        "/data/adb/ivanna_omega/omega_control_snapshot";
    static constexpr size_t REGION_SIZE = 4096; // una página

    OmegaControlBus() = default;
    ~OmegaControlBus() { close(); }

    // No copiable (gestiona un fd + puntero mmap)
    OmegaControlBus(const OmegaControlBus&) = delete;
    OmegaControlBus& operator=(const OmegaControlBus&) = delete;

    // ── Writer side (daemon) ──────────────────────────────────────────────────

    /** Crea/abre el archivo SHM y lo mapea con permisos de escritura.
     *  Inicializa con un snapshot default si el archivo es nuevo.
     *  @return true si listo para publish(). */
    bool openWriter(const char* path = DEFAULT_PATH) noexcept;

    /** Publica un snapshot de forma atómica (seqlock).
     *  Incrementa generation, calcula CRC, escribe bajo seqlock.
     *  Seguro llamar desde cualquier hilo serializado (command_server usa mutex). */
    bool publish(const OmegaDspSnapshot& snap) noexcept;

    /** Generación más reciente publicada (para reportar en respuestas JSON). */
    uint64_t lastPublishedGeneration() const noexcept { return m_lastGeneration; }

    // ── Reader side (omega_effect / audioserver) ──────────────────────────────

    /** Abre el archivo SHM y lo mapea en solo-lectura.
     *  Safe to call from constructor de omega_effect; nunca bloquea el audio callback.
     *  @return true si listo para readLatest(). */
    bool openReader(const char* path = DEFAULT_PATH) noexcept;

    /** Lee el snapshot más reciente de forma lock-free.
     *  Solo actualiza 'out' si hay un nuevo snapshot (generation > lastSeenGen).
     *  Si el snapshot está corrupto, deja 'out' sin cambios y retorna false.
     *  @param out          Destino del snapshot leído.
     *  @param lastSeenGen  IN/OUT: última generation vista por el lector.
     *  @return true si 'out' fue actualizado con un snapshot válido y nuevo. */
    bool readLatest(OmegaDspSnapshot& out, uint64_t& lastSeenGen) const noexcept;

    // ── Estado general ────────────────────────────────────────────────────────

    bool isWriterOpen() const noexcept { return m_region && m_isWriter; }
    bool isReaderOpen() const noexcept { return m_region && !m_isWriter; }
    bool isOpen()       const noexcept { return m_region != nullptr; }

    void close() noexcept;

private:
    SharedControlRegion* m_region     = nullptr;
    int                  m_fd         = -1;
    bool                 m_isWriter   = false;
    uint64_t             m_lastGeneration = 0;
};

// ── Singleton del daemon (writer) ──────────────────────────────────────────

inline OmegaControlBus& controlBus() noexcept {
    static OmegaControlBus instance;
    return instance;
}

// ── Path del bus LOCAL del efecto (Ruta B sin daemon) ───────────────────────
// Fuente única de verdad: la escribe omega_effect.cpp (writer, proceso
// audioserver) y la lee audioRouteBridgeLoop() (reader, proceso app) cuando
// omega_daemon_get_shared_state() es stub (sin root / sin daemon).
// /data/local/tmp es escribible por shell/system y legible cross-process con
// SELinux permissive en userdebug; si el DAC/SELinux lo bloquea en un
// dispositivo concreto, el reader simplemente no abre y la UI sigue OFFLINE
// (mismo comportamiento que antes del fallback — no hay regresión).
inline constexpr const char* OMEGA_EFFECT_LOCAL_BUS_PATH =
    "/data/local/tmp/omega_effect_ctrl_local";

// ── Singleton del effect (reader) — proceso audioserver ────────────────────
// Separado del writer para evitar confusiones de proceso.
// En la práctica, el reader vive en omega_effect.cpp (proceso audioserver)
// y el writer en el daemon. Ambos llaman a openReader/openWriter respectivamente.
// El singleton es SOLO para el proceso actual.
inline OmegaControlBus& effectControlBus() noexcept {
    static OmegaControlBus instance;
    return instance;
}

} // namespace ivanna
