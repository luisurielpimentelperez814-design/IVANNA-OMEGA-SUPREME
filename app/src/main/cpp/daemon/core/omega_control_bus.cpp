/*
 * omega_control_bus.cpp — Implementación de OmegaControlBus
 *
 * Writer (daemon process):  crea/abre SHM, publica snapshots atómicos.
 * Reader (audioserver):     mapea SHM en read-only, lee sin bloqueo.
 *
 * Validado en host (g++/clang++ sin NDK). Integración ARM64: ver CI gate.
 */

#include "../../include/omega_control_bus.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

namespace ivanna {

// ── Helpers internos ──────────────────────────────────────────────────────────

static uint64_t nowMs() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1'000'000ULL);
}

// ── Writer side ───────────────────────────────────────────────────────────────

bool OmegaControlBus::openWriter(const char* path) noexcept {
    if (m_region) return true; // ya abierto

    // Crear el archivo SHM (rw para propietario = root/daemon)
    int fd = ::open(path, O_RDWR | O_CREAT, 0660);
    if (fd < 0) {
        OMEGA_CTRL_LOGW("openWriter: open(%s) failed: %s", path, strerror(errno));
        return false;
    }

    // Asegurar tamaño mínimo de REGION_SIZE bytes
    struct stat st{};
    if (::fstat(fd, &st) == 0 && (size_t)st.st_size < REGION_SIZE) {
        if (::ftruncate(fd, (off_t)REGION_SIZE) < 0) {
            OMEGA_CTRL_LOGW("openWriter: ftruncate failed: %s", strerror(errno));
            ::close(fd);
            return false;
        }
    }

    void* addr = ::mmap(nullptr, REGION_SIZE,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        OMEGA_CTRL_LOGW("openWriter: mmap failed: %s", strerror(errno));
        ::close(fd);
        return false;
    }

    m_fd       = fd;
    m_region   = reinterpret_cast<SharedControlRegion*>(addr);
    m_isWriter = true;

    // Si el SHM es nuevo (magic inválido), escribir snapshot default
    if (m_region->snapshot.magic != OMEGA_CTRL_MAGIC) {
        auto def = OmegaDspSnapshot::makeDefault();
        // Escribir sin seqlock: nadie más puede estar leyendo un SHM nuevo
        std::memcpy(&m_region->snapshot, &def, sizeof(def));
        m_region->guard.store(0, std::memory_order_release);
        OMEGA_CTRL_LOGD("openWriter: initialized new SHM at %s", path);
    } else {
        OMEGA_CTRL_LOGD("openWriter: attached existing SHM at %s (gen=%llu)",
                        path,
                        (unsigned long long)m_region->snapshot.generation);
    }

    m_lastGeneration = m_region->snapshot.generation;
    return true;
}

bool OmegaControlBus::publish(const OmegaDspSnapshot& snap) noexcept {
    if (!m_region || !m_isWriter) return false;

    // Copiar localmente para poder mutar generation, timestamp y crc32
    OmegaDspSnapshot local = snap;
    local.magic     = OMEGA_CTRL_MAGIC;
    local.version   = OMEGA_CTRL_VERSION;
    local.generation = m_lastGeneration + 1;
    local.timestamp_ms = nowMs();
    local.stampCrc();

    // Seqlock write: guard odd durante escritura, even en reposo
    // guard es std::atomic<uint32_t>: los fetch_add son los fences
    m_region->guard.fetch_add(1, std::memory_order_acq_rel); // odd → escritura
    std::memcpy(&m_region->snapshot, &local, sizeof(local));
    m_region->guard.fetch_add(1, std::memory_order_release); // even → estable

    m_lastGeneration = local.generation;
    return true;
}

// ── Reader side ───────────────────────────────────────────────────────────────

bool OmegaControlBus::openReader(const char* path) noexcept {
    if (m_region) return true; // ya abierto

    // Solo lectura: audioserver no tiene permiso de escritura al archivo del daemon
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        OMEGA_CTRL_LOGD("openReader: %s not available yet (daemon not running?): %s",
                         path, strerror(errno));
        return false;
    }

    void* addr = ::mmap(nullptr, REGION_SIZE,
                        PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        OMEGA_CTRL_LOGW("openReader: mmap failed: %s", strerror(errno));
        ::close(fd);
        return false;
    }

    m_fd       = fd;
    m_region   = reinterpret_cast<SharedControlRegion*>(addr);
    m_isWriter = false;

    OMEGA_CTRL_LOGD("openReader: mapped SHM from %s (gen=%llu)",
                    path,
                    (unsigned long long)m_region->snapshot.generation);
    return true;
}

bool OmegaControlBus::readLatest(OmegaDspSnapshot& out,
                                  uint64_t& lastSeenGen) const noexcept {
    if (!m_region) return false;

    // Seqlock read: reintentar si guard es odd (escritura en curso)
    // o si guard cambió entre las dos lecturas (torn read).
    OmegaDspSnapshot snap;
    uint32_t g1, g2;

    // Límite de reintentos: en un lector de audio el writer daemon es un
    // proceso separado. En el peor caso (escritura dura ~1 µs) con 32
    // iteraciones cubrimos cualquier escenario real sin bloquear el thread.
    for (int retries = 0; retries < 32; ++retries) {
        g1 = m_region->guard.load(std::memory_order_acquire);
        if (g1 & 1u) continue;             // escritura en curso

        std::memcpy(&snap, &m_region->snapshot, sizeof(snap));

        g2 = m_region->guard.load(std::memory_order_acquire);
        if (g1 == g2) goto consistent;     // lectura consistente
    }
    // Si llegamos aquí tras 32 reintentos, el writer está bajo presión extrema.
    // No actualizar 'out': usar último snapshot válido (o default del caller).
    return false;

consistent:
    // Nada nuevo
    if (snap.generation == lastSeenGen) return false;

    // Validar integridad antes de aceptar
    if (!snap.isValid()) {
        OMEGA_CTRL_LOGW("readLatest: snapshot inválido (gen=%llu magic=%08X crc_ok=%d) — usando último válido",
                        (unsigned long long)snap.generation,
                        snap.magic,
                        (int)snap.isCrcValid());
        return false;
    }

    out         = snap;
    lastSeenGen = snap.generation;
    return true;
}

void OmegaControlBus::close() noexcept {
    if (m_region) {
        ::munmap(m_region, REGION_SIZE);
        m_region = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_isWriter = false;
    m_lastGeneration = 0;
}

} // namespace ivanna
