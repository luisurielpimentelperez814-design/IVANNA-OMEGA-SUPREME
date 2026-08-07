#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// daemon/core/shm_manager.h
//
// OmegaShmManager — región de memoria compartida entre ivanna_daemon y la app.
//
// El daemon crea un archivo backing en OMEGA_SHM_PATH, lo trunca a SHM_SIZE,
// lo mapea con MAP_SHARED y mlockea la región para evitar swapping en el
// hilo de audio. La app Kotlin la accede vía android.os.SharedMemory (API 27+)
// recibiendo el fd por SCM_RIGHTS.
//
// Diseño intencional:
//   · Un solo buffer lineal de 64 KiB (16 UnifiedControlFrames de ~4 KiB c/u).
//   · El daemon escribe en offset 0; la app lee desde el mismo offset.
//   · Acceso lock-free: escritor atómico 64-bit (seqlock epoch) en los
//     primeros 8 bytes; el lector verifica epoch antes y después de copiar.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstdint>
#include <string>

namespace ivanna {

// Tamaño de la región SHM (64 KiB — 16 frames × ~4 KiB)
inline constexpr size_t SHM_SIZE = 65536;

// Layout de los primeros 16 bytes (seqlock header)
struct alignas(8) ShmHeader {
    std::atomic<uint64_t> epoch;     // seqlock epoch: par = estable, impar = escribiendo
    uint32_t              frame_len; // longitud del frame serializado en bytes
    uint32_t              reserved;
};

class OmegaShmManager {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────
    /**
     * Abre o crea el archivo backing en @p path, lo trunca a SHM_SIZE y lo
     * mapea con MAP_SHARED | PROT_READ | PROT_WRITE. Llama a mlock() para
     * fijar la región en RAM.
     *
     * @return true si la región quedó mapeada y bloqueada.
     */
    bool init(const std::string& path);

    /** Desmapea y cierra el fd. Idempotente. */
    void close();

    // ── Acceso al buffer ──────────────────────────────────────────────────────
    /** Puntero al inicio de la región SHM, o nullptr si no inicializado. */
    void*  base()    const noexcept { return m_base; }
    int    fd()      const noexcept { return m_fd;   }
    size_t size()    const noexcept { return m_size;  }
    bool   isReady() const noexcept { return m_base != nullptr; }

    // ── Seqlock helpers (solo daemon-writer side) ────────────────────────────
    /**
     * Escribe @p len bytes de @p src en el buffer SHM con protocolo seqlock:
     *   1. Incrementa epoch a impar (inicio de escritura).
     *   2. Copia datos a (base + sizeof(ShmHeader)).
     *   3. Actualiza frame_len.
     *   4. Incrementa epoch a par (escritura completa).
     *
     * Los lectores verifican epoch antes y después de copiar; si es impar
     * o cambió, repiten la lectura.
     *
     * @return false si la región no está lista o @p len > SHM_SIZE - sizeof(ShmHeader).
     */
    bool write(const void* src, size_t len) noexcept;

    ~OmegaShmManager() { close(); }

    // No copyable
    OmegaShmManager() = default;
    OmegaShmManager(const OmegaShmManager&) = delete;
    OmegaShmManager& operator=(const OmegaShmManager&) = delete;

private:
    int    m_fd   = -1;
    void*  m_base = nullptr;
    size_t m_size = 0;
};

// ── Daemon singleton ──────────────────────────────────────────────────────────
inline OmegaShmManager& shmManager() {
    static OmegaShmManager instance;
    return instance;
}

} // namespace ivanna
