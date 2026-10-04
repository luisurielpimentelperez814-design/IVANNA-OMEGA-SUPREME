// omega_effect_beacon.h — señal REAL de que omega_effect.so está procesando audio.
//
// Problema que cierra (AGENT_CLAIMS.md, "Doble Procesamiento Ruta A+B", 2026-09-24):
// isDaemonRunning() solo prueba que el proceso daemon está vivo; NO prueba que
// omega_effect esté insertado ni procesando en audioserver. Gates basados en esa señal
// (bloquear Ruta A, apagar el fallback local) dejaban a IVANNA sin sonido.
// OmegaDspSnapshot::effect_frames no sirve: con el daemon activo el efecto es solo
// reader del bus del daemon y nunca publica en el bus local, así que la app no lo ve.
//
// Contrato: el efecto (writer, audioserver) mantiene un bloque de 64 B mmap'd en
// OMEGA_EFFECT_BEACON_PATH. onEnable/onDisable/open son NO-RT; onBlock es RT-safe
// (atómicos relajados + clock_gettime vDSO, sin locks ni syscalls). El lector (app) lo
// clasifica con classifyEffectBeacon(); si el archivo no se puede abrir (DAC/SELinux)
// devuelve Unavailable y el llamador DEBE conservar su comportamiento previo.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ivanna {

inline constexpr const char* OMEGA_EFFECT_BEACON_PATH = "/data/local/tmp/omega_effect_beacon";
inline constexpr uint32_t    OMEGA_EFFECT_BEACON_MAGIC = 0x4F454246u;  // "OEBF"
inline constexpr size_t      OMEGA_EFFECT_BEACON_SIZE  = 64;

struct alignas(8) EffectBeacon {
    std::atomic<uint32_t> magic;          // OMEGA_EFFECT_BEACON_MAGIC cuando es válido
    std::atomic<uint32_t> enabled_count;  // instancias del efecto con EFFECT_CMD_ENABLE activo
    std::atomic<uint64_t> frames;         // frames procesados por el DSP completo (monótono)
    std::atomic<uint64_t> last_block_ms;  // CLOCK_MONOTONIC del último bloque procesado
    std::atomic<uint64_t> stamp_ms;       // CLOCK_MONOTONIC de la última actividad (enable/bloque)
};
static_assert(sizeof(EffectBeacon) <= OMEGA_EFFECT_BEACON_SIZE, "EffectBeacon excede 64 B");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "se requieren atómicos de 64 bit lock-free");

// Estado visible a la app. Unavailable obliga al llamador a NO cambiar su comportamiento.
enum class EffectBeaconState : int {
    Unavailable = -1,  // sin archivo / sin permiso / formato inválido
    NoEffect    = 0,   // ninguna instancia habilitada (o dato de un arranque anterior)
    EnabledIdle = 1,   // habilitado pero sin bloques recientes (pausa/silencio sin stream)
    Processing  = 2,   // bloques procesados dentro de la ventana idleMs
};

inline uint64_t effectBeaconNowMs() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
}

// Clasificación pura (testeable en host, sin I/O).
inline EffectBeaconState classifyEffectBeacon(uint32_t enabledCount, uint64_t lastBlockMs,
                                              uint64_t stampMs, uint64_t nowMs,
                                              uint64_t idleMs) noexcept {
    // stamp en el futuro => el archivo viene de un arranque anterior (CLOCK_MONOTONIC reinició).
    if (stampMs > nowMs || lastBlockMs > nowMs) return EffectBeaconState::NoEffect;
    if (enabledCount == 0u) return EffectBeaconState::NoEffect;
    if (lastBlockMs != 0u && (nowMs - lastBlockMs) <= idleMs) return EffectBeaconState::Processing;
    return EffectBeaconState::EnabledIdle;
}

// ── Lado lector (proceso app). Abre/cierra por llamada: se invoca a baja frecuencia. ──
inline EffectBeaconState readEffectBeacon(uint64_t idleMs = 2000ull,
                                          const char* path = OMEGA_EFFECT_BEACON_PATH,
                                          uint64_t* outFrames = nullptr) noexcept {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return EffectBeaconState::Unavailable;
    struct stat st{};
    if (::fstat(fd, &st) < 0 || static_cast<size_t>(st.st_size) < OMEGA_EFFECT_BEACON_SIZE) {
        ::close(fd);
        return EffectBeaconState::Unavailable;
    }
    void* addr = ::mmap(nullptr, OMEGA_EFFECT_BEACON_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (addr == MAP_FAILED) return EffectBeaconState::Unavailable;
    const auto* b = static_cast<const EffectBeacon*>(addr);
    EffectBeaconState res = EffectBeaconState::Unavailable;
    if (b->magic.load(std::memory_order_acquire) == OMEGA_EFFECT_BEACON_MAGIC) {
        if (outFrames) *outFrames = b->frames.load(std::memory_order_relaxed);
        res = classifyEffectBeacon(b->enabled_count.load(std::memory_order_relaxed),
                                   b->last_block_ms.load(std::memory_order_relaxed),
                                   b->stamp_ms.load(std::memory_order_relaxed),
                                   effectBeaconNowMs(), idleMs);
    }
    ::munmap(addr, OMEGA_EFFECT_BEACON_SIZE);
    return res;
}

// ── Lado escritor (audioserver). Un único mapeo por proceso, compartido por instancias. ──
class EffectBeaconWriter {
public:
    // NO-RT. Idempotente. false si el archivo no se puede crear/mapear (se degrada en silencio).
    bool open(const char* path = OMEGA_EFFECT_BEACON_PATH) noexcept {
        if (m_beacon.load(std::memory_order_acquire)) return true;
        // Varias instancias del efecto (sesiones AudioFlinger) pueden llegar aqui a la vez desde
        // hilos distintos: sin exclusion, ambas mapearian y la segunda pondria a 0 el contador que
        // la primera ya incremento. open() es NO-RT, asi que un mutex aqui es seguro.
        std::lock_guard<std::mutex> lk(m_openMtx);
        if (m_beacon.load(std::memory_order_acquire)) return true;
        const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        if (fd < 0) return false;
        ::fchmod(fd, 0666);  // la app (otro uid) debe poder leerlo
        if (::ftruncate(fd, OMEGA_EFFECT_BEACON_SIZE) < 0) { ::close(fd); return false; }
        void* addr = ::mmap(nullptr, OMEGA_EFFECT_BEACON_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (addr == MAP_FAILED) return false;
        auto* b = static_cast<EffectBeacon*>(addr);
        // Primer open de este proceso: descartar contadores de un audioserver anterior.
        b->enabled_count.store(0u, std::memory_order_relaxed);
        b->frames.store(0ull, std::memory_order_relaxed);
        b->last_block_ms.store(0ull, std::memory_order_relaxed);
        b->stamp_ms.store(effectBeaconNowMs(), std::memory_order_relaxed);
        b->magic.store(OMEGA_EFFECT_BEACON_MAGIC, std::memory_order_release);
        m_beacon.store(b, std::memory_order_release);
        return true;
    }
    // NO-RT (EFFECT_CMD_ENABLE, solo en la transición deshabilitado -> habilitado).
    void onEnable() noexcept {
        if (!open()) return;
        EffectBeacon* b = m_beacon.load(std::memory_order_acquire);
        b->enabled_count.fetch_add(1u, std::memory_order_relaxed);
        b->stamp_ms.store(effectBeaconNowMs(), std::memory_order_relaxed);
    }
    // NO-RT (DISABLE/release, solo en la transición habilitado -> deshabilitado). Sin underflow.
    void onDisable() noexcept {
        EffectBeacon* b = m_beacon.load(std::memory_order_acquire);
        if (!b) return;
        uint32_t c = b->enabled_count.load(std::memory_order_relaxed);
        while (c > 0u && !b->enabled_count.compare_exchange_weak(c, c - 1u, std::memory_order_relaxed)) {}
        b->stamp_ms.store(effectBeaconNowMs(), std::memory_order_relaxed);
    }
    // RT-safe: un bloque pasó por TODO el DSP.
    void onBlock(uint32_t frames) noexcept {
        EffectBeacon* b = m_beacon.load(std::memory_order_acquire);
        if (!b) return;
        const uint64_t now = effectBeaconNowMs();
        b->frames.fetch_add(frames, std::memory_order_relaxed);
        b->last_block_ms.store(now, std::memory_order_relaxed);
        b->stamp_ms.store(now, std::memory_order_relaxed);
    }
private:
    std::atomic<EffectBeacon*> m_beacon{nullptr};
    std::mutex m_openMtx;
};

inline EffectBeaconWriter& effectBeacon() noexcept {
    static EffectBeaconWriter w;
    return w;
}

} // namespace ivanna
