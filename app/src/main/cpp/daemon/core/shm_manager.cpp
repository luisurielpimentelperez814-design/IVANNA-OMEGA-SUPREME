// daemon/core/shm_manager.cpp
// Implementación de OmegaShmManager para el proceso daemon.
//
// Responsabilidades de este módulo vs ivanna_daemon.cpp:
//   · ivanna_daemon.cpp — crea el archivo SHM en initialize_shared_memory()
//     y entrega su fd a la app por SCM_RIGHTS (handshake "Modo B", real
//     desde commit 78aed525: cliente que conecta a @omega_daemon_socket y
//     calla 150 ms recibe el fd por sendmsg). No se modifica aquí.
//   · shm_manager.cpp — API de alto nivel reutilizable para escritura
//     seqlock (write/writeControl) y métricas de salud del canal
//     (bumpHealthCounter) que command_server.cpp y el loop principal del
//     daemon usan sin duplicar el boilerplate de mmap/mlock/seqlock.
//
// Por qué dos implementaciones coexisten:
//   initialize_shared_memory() en ivanna_daemon.cpp mapea el mismo archivo
//   backing (OMEGA_SHM_PATH) con ftruncate + mmap inline. OmegaShmManager
//   puede reusar ese mismo fd/ptr o inicializarse de forma independiente.
//   En esta versión se inicializa de forma independiente para preservar
//   la compatibilidad sin tocar ivanna_daemon.cpp.

#include "shm_manager.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Log portátil: android/log.h solo existe bajo NDK. En host (tests CTest del
// CI, sondas de layout) se degrada a stderr — mismo patrón que
// include/omega_control_bus.h (OMEGA_CTRL_LOGD), sin tocar el código vivo.
#if defined(__ANDROID__)
#  include <android/log.h>
#  define SHM_TAG "IVANNA_SHM"
#  define SHM_LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, SHM_TAG, fmt, ##__VA_ARGS__)
#else
#  include <cstdio>
#  define SHM_LOG(fmt, ...) std::fprintf(stderr, "[IVANNA_SHM] " fmt "\n", ##__VA_ARGS__)
#endif

namespace ivanna {

bool OmegaShmManager::init(const std::string& path) {
    if (m_base != nullptr) {
        SHM_LOG("init() ignorado: ya inicializado en %s", path.c_str());
        return true;
    }

    // Crear directorios si no existen
    // Android: /data/adb/ivanna_omega/ debe existir — el módulo Magisk lo crea.
    // Aquí solo abrimos/creamos el archivo backing.
    m_fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0660);
    if (m_fd < 0) {
        SHM_LOG("open(%s) error: %s", path.c_str(), strerror(errno));
        return false;
    }

    // Asegurar tamaño mínimo
    struct stat st{};
    if (fstat(m_fd, &st) == 0 && static_cast<size_t>(st.st_size) < SHM_SIZE) {
        if (ftruncate(m_fd, static_cast<off_t>(SHM_SIZE)) != 0) {
            SHM_LOG("ftruncate error: %s", strerror(errno));
            ::close(m_fd);
            m_fd = -1;
            return false;
        }
    }

    // MAP_SHARED: visible a cualquier proceso que mapee el mismo fd
    void* ptr = mmap(nullptr, SHM_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, m_fd, 0);
    if (ptr == MAP_FAILED) {
        SHM_LOG("mmap error: %s", strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    // mlock: fijar en RAM para evitar page-fault en el hilo de audio
    if (mlock(ptr, SHM_SIZE) != 0) {
        // No fatal — degradar a non-locked (advertencia)
        SHM_LOG("mlock advertencia: %s (no fatal)", strerror(errno));
    }

    m_base = ptr;
    m_size = SHM_SIZE;

    // Inicializar header seqlock a epoch=0 (lector ve estado limpio)
    auto* hdr = static_cast<ShmHeader*>(m_base);
    hdr->epoch.store(0, std::memory_order_release);
    hdr->frame_len  = 0;
    hdr->magic      = OMEGA_SHM_MAGIC;
    hdr->version    = OMEGA_SHM_VERSION;
    hdr->state_size = static_cast<uint32_t>(sizeof(OmegaSharedState));
    hdr->reserved   = 0;

      SHM_LOG("SHM listo: %s (%llu bytes, mapeado en %p)",
              path.c_str(),
              static_cast<unsigned long long>(SHM_SIZE),
              ptr);
    return true;
}

void OmegaShmManager::close() {
    if (m_base) {
        munlock(m_base, m_size);
        munmap(m_base, m_size);
        m_base = nullptr;
        m_size = 0;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool OmegaShmManager::write(const void* src, size_t len) noexcept {
    return writeControl(0, src, len);
}

void OmegaShmManager::bumpHealthCounter(uint32_t index) noexcept {
    if (!m_base || index > 4) return; // 5 contadores + 1 reservado en el bloque
    auto* counter = reinterpret_cast<std::atomic<uint32_t>*>(
        static_cast<uint8_t*>(m_base) + sizeof(ShmHeader) + SHM_HEALTH_OFFSET + index * 4);
    counter->fetch_add(1u, std::memory_order_relaxed);
}

bool OmegaShmManager::writeControl(size_t offset, const void* src, size_t len) noexcept {
    if (!m_base || !src) { noteRejectedWrite(); return false; }

    constexpr size_t kHeaderSize = sizeof(ShmHeader);
    if (offset + len > SHM_STATE_OFFSET - kHeaderSize) { // frames SOLO en la pagina de control: nunca solapan OmegaSharedState @ SHM_STATE_OFFSET
        noteRejectedWrite();
        return false;
    }

    auto* hdr  = static_cast<ShmHeader*>(m_base);
    auto* data = static_cast<uint8_t*>(m_base) + kHeaderSize;

    // Seqlock write protocol (single-writer: solo el daemon escribe; el
    // heartbeat thread y el accept loop llaman bumpHealthCounter, que NO
    // toca epoch — solo writeControl entra aqui, y solo desde el loop
    // principal del daemon o el heartbeat thread, nunca concurrentes entre
    // si porque el heartbeat thread no llama writeControl desde 2026-09-17).
    // Fences estandar C++11 (no __sync_synchronize legacy GCC):
    //   1. epoch -> impar (release: visible antes de los datos)
    //   2. fence seq_cst (los datos no se reordenan antes del store impar)
    //   3. copiar datos
    //   4. actualizar frame_len
    //   5. fence seq_cst (los datos no se reordenan despues del store par)
    //   6. epoch -> par (release: escritura completa visible)
    //   Lectores que leen epoch impar o ven cambio entre pre/post repiten.
    const uint64_t seq = hdr->epoch.load(std::memory_order_relaxed);
    hdr->epoch.store(seq | 1ULL, std::memory_order_release);  // impar
    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::memcpy(data + offset, src, len);
    // frame_len documenta la longitud del frame de DATOS (canal SAF, offset 0).
    // FIX (semantica): el heartbeat (offset SHM_HEARTBEAT_OFFSET, len=8) tambien
    // pasaba por aqui y sobrescribia frame_len con 8 cada segundo, dejando el
    // campo mintiendo sobre la longitud del frame SAF (16B) que es lo unico que
    // los lectores del header interpretan como "frame". Solo el canal de datos
    // (offset 0) actualiza frame_len; los frames auxiliares (heartbeat, futuros)
    // llevan su longitud implicita en su offset fijo del ABI.
    if (offset == 0)
        hdr->frame_len = static_cast<uint32_t>(len);

    std::atomic_thread_fence(std::memory_order_seq_cst);
    hdr->epoch.store(seq + 2ULL, std::memory_order_release);  // par (+2)

    return true;
}


// ── Metricas extendidas + shutdown marker (layout v2.1) ────────────────────
// Escritor unico: el daemon. Lectores: app (panel), health_check.sh.
// Campos alineados de un solo productor -> stores atomicos bastan, sin seqlock.

static inline void storeU32(void* base, size_t off, uint32_t v) noexcept {
    auto* p = reinterpret_cast<std::atomic<uint32_t>*>(
        static_cast<uint8_t*>(base) + off);
    p->store(v, std::memory_order_release);
}
static inline uint32_t loadU32(void* base, size_t off) noexcept {
    auto* p = reinterpret_cast<std::atomic<uint32_t>*>(
        static_cast<uint8_t*>(base) + off);
    return p->load(std::memory_order_acquire);
}

void OmegaShmManager::publishUptime(uint64_t seconds) noexcept {
    if (!m_base) return;
    auto* p = reinterpret_cast<std::atomic<uint64_t>*>(
        static_cast<uint8_t*>(m_base) + SHM_UPTIME_OFFSET);
    p->store(seconds, std::memory_order_release);
    storeU32(m_base, SHM_METRICS_MAGIC_OFFSET, OMEGA_METRICS_MAGIC);
}

void OmegaShmManager::noteClientsPeak(uint32_t current) noexcept {
    if (!m_base) return;
    if (current > loadU32(m_base, SHM_CLIENTS_PEAK_OFFSET))
        storeU32(m_base, SHM_CLIENTS_PEAK_OFFSET, current);
}

void OmegaShmManager::noteCommandResult(bool ok) noexcept {
    if (!m_base) return;
    const size_t off = ok ? SHM_CMDS_OK_OFFSET : SHM_CMDS_ERR_OFFSET;
    storeU32(m_base, off, loadU32(m_base, off) + 1);
}

void OmegaShmManager::publishRssKb(uint32_t kb) noexcept {
    if (!m_base) return;
    storeU32(m_base, SHM_RSS_KB_OFFSET, kb);
}

void OmegaShmManager::markShutdownClean(bool clean) noexcept {
    if (!m_base) return;
    storeU32(m_base, SHM_SHUTDOWN_CLEAN_OFFSET, clean ? 1u : 0u);
    storeU32(m_base, SHM_METRICS_MAGIC_OFFSET, OMEGA_METRICS_MAGIC);
}

bool OmegaShmManager::previousShutdownClean() noexcept {
    if (!m_base) return true;  // sin SHM no hay evidencia de crash
    if (loadU32(m_base, SHM_METRICS_MAGIC_OFFSET) != OMEGA_METRICS_MAGIC)
        return true;           // primer arranque tras el upgrade: sin marca
    return loadU32(m_base, SHM_SHUTDOWN_CLEAN_OFFSET) == 1u;
}

void OmegaShmManager::noteCrashDetected() noexcept {
    if (!m_base) return;
    storeU32(m_base, SHM_CRASH_COUNT_OFFSET, loadU32(m_base, SHM_CRASH_COUNT_OFFSET) + 1);
}

bool OmegaShmManager::extendedMetricsPresent() noexcept {
    if (!m_base) return false;
    return loadU32(m_base, SHM_METRICS_MAGIC_OFFSET) == OMEGA_METRICS_MAGIC;
}

} // namespace ivanna
