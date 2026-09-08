#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// daemon/core/shm_manager.h
//
// OmegaShmManager — región de memoria compartida entre ivanna_daemon y la app.
//
// El daemon crea un archivo backing en OMEGA_SHM_PATH, lo trunca a SHM_SIZE,
// lo mapea con MAP_SHARED y mlockea la región para evitar swapping en el
// hilo de audio. La app Kotlin la accede vía android.os.SharedMemory (API 27+)
// mapeando el mismo backing file por ruta fija (/data/adb/ivanna_omega/omega_shm). NOTA: no existe sendmsg/SCM_RIGHTS en el daemon — cualquier referencia a paso de fd por socket es obsoleta.
//
// Diseño intencional:
//   · Un solo buffer lineal de 64 KiB (16 UnifiedControlFrames de ~4 KiB c/u).
//   · El daemon escribe en offset 0; la app lee desde el mismo offset.
//   · Acceso lock-free: escritor atómico 64-bit (seqlock epoch) en los
//     primeros 8 bytes; el lector verifica epoch antes y después de copiar.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>    // FIX: std::atomic<uint64_t> en ShmHeader lo requiere;
                     // sin este include cualquier TU que incluya shm_manager.h
                     // directamente fallaba con "std::atomic no declarado".
#include <cstddef>
#include <cstdint>
#include <string>
#include "omega_shared.h"  // sizeof(OmegaSharedState): fuente unica de tamano

namespace ivanna {

// ── Layout unificado de la region SHM ──────────────────────────────────────
// [ ShmHeader (32B, seqlock de control) | OmegaSharedState @4096 | frames | margen ]
// Antes: SHM_SIZE=65536 pero sizeof(OmegaSharedState)=131272 -> el placement-new
// del daemon escribia ~64KB FUERA del mmap (overflow real). Ahora la constante
// unica deriva del tipo y se alinea a pagina con margen para frames de control.
inline constexpr uint32_t OMEGA_SHM_MAGIC   = 0x4F4D4547u;  // "OMEG"
inline constexpr uint32_t OMEGA_SHM_VERSION = 2u;           // layout v2
inline constexpr size_t   SHM_STATE_OFFSET  = 4096;         // pagina 0: control
// Layout de la página de control (pagina 0), tras ShmHeader:
//   [+0  ] SAF frame 16B (gain/compressor/exciter/spatial)
//   [+16 ] heartbeat 8B — monotonic ms, escrito por el daemon en cada iteración
// Offsets FIJOS: los lee ShmManager.kt. Cambiarlos exige bump coordinado.
inline constexpr size_t   SHM_SAF_FRAME_OFFSET   = 0;   // relativo a base+sizeof(ShmHeader)
inline constexpr size_t   SHM_HEARTBEAT_OFFSET   = 16;  // idem
inline constexpr size_t   SHM_CONTROL_BYTES = 16384;        // reserva p/ frames
inline constexpr size_t   SHM_SIZE_RAW      = SHM_STATE_OFFSET + sizeof(OmegaSharedState) + SHM_CONTROL_BYTES;
inline constexpr size_t   SHM_SIZE          = (SHM_SIZE_RAW + 4095) & ~size_t(4095); // alineado a pagina
static_assert(SHM_SIZE >= SHM_STATE_OFFSET + sizeof(OmegaSharedState),
              "SHM_SIZE no cubre OmegaSharedState");

// Layout de los primeros 32 bytes (seqlock header)
struct alignas(8) ShmHeader {
    std::atomic<uint64_t> epoch;     // seqlock epoch: par = estable, impar = escribiendo
    uint32_t              frame_len; // longitud del frame serializado en bytes
    uint32_t              magic;     // OMEGA_SHM_MAGIC — detecta memoria incompatible
    uint32_t              version;   // OMEGA_SHM_VERSION — layout del protocolo
    uint32_t              state_size;// sizeof(OmegaSharedState) esperado
    uint32_t              reserved;
};
// Contrato de layout fijado (ABI SHM v2): Kotlin lee el frame de control en
// base+sizeof(ShmHeader). Si esta estructura cambia de tamaño, el build FALLA
// aquí en vez de desalinear silenciosamente al reader. ShmManager.kt debe
// mantener SHM_HEADER_BYTES=32 sincronizado con este assert.
static_assert(sizeof(ShmHeader) == 32,
              "ShmHeader ABI mismatch: expected 32 bytes");
// Offsets exactos que el reader Kotlin hardcodea (ShmManager.kt):
// magic@12, version@16, state_size@20. Si el struct se reordena, estos
// asserts truenan en build en vez de desalinear silenciosamente al reader.
static_assert(offsetof(ShmHeader, magic)      == 12, "ShmHeader.magic debe estar en +12");
static_assert(offsetof(ShmHeader, version)    == 16, "ShmHeader.version debe estar en +16");
static_assert(offsetof(ShmHeader, state_size) == 20, "ShmHeader.state_size debe estar en +20");

// ── Validación canónica del header (FASE 5: magic/version/state_size/bounds) ──
// UNICA fuente de verdad para "¿este mmap es una region SHM nuestra y sana?".
// La usan el bridge C++ de la app (omega_daemon_bridge_stub.cpp), los tests
// host y cualquier futuro reader. Kotlin replica la misma lógica con los
// offsets absolutos de arriba (no puede incluir este header).
//
// Por qué existe: mapear un backing file viejo, truncado o de otra versión
// y reinterpretarlo como OmegaSharedState vivo producía lecturas de basura
// indistinguibles de telemetría real. Todo reader debe validar ANTES de
// confiar en cualquier byte de la región.
inline bool validateShmHeader(const void* base, size_t mappedLen) noexcept {
    if (base == nullptr || mappedLen < sizeof(ShmHeader)) return false;
    const auto* hdr = static_cast<const ShmHeader*>(base);
    if (hdr->magic   != OMEGA_SHM_MAGIC)   return false;  // backing ajeno/corrupto
    if (hdr->version != OMEGA_SHM_VERSION) return false;  // layout de otra época
    if (hdr->state_size != static_cast<uint32_t>(sizeof(OmegaSharedState)))
        return false;                                     // struct de otro build
    return true;
}

// ── FASE 3: atomicidad cross-process (respuesta formal de la auditoría) ──────
// placement-new de std::atomic sobre memoria mmap compartida es válido SOLO si
// el atómico es lock-free: un atómico no-lock-free puede llevar un mutex
// interno cuyo estado vive en la memoria compartida, y pthread_mutex en mmap
// sin PTHREAD_PROCESS_SHARED es UB entre procesos. En ARM64/ARMv8.1 (LSE) y
// ARMv7 con KUSER helpers, los atómicos de <=64 bits son lock-free — pero eso
// es una propiedad del TARGET, no del código. Si alguien compila para un ABI
// donde deje de cumplirse (o añade un atómico más grande, p.ej. u128), el
// seqlock se volvería UB inter-proceso EN SILENCIO. Estos asserts lo convierten
// en error de build.
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "epoch (u64) debe ser lock-free: seqlock inter-proceso depende de ello");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "atomicos u32 del estado compartido deben ser lock-free");
static_assert(std::atomic<float>::is_always_lock_free,
              "atomicos float del estado compartido deben ser lock-free");
static_assert(std::atomic<bool>::is_always_lock_free,
              "atomicos bool del estado compartido deben ser lock-free");
static_assert(std::atomic<int>::is_always_lock_free,
              "atomicos int del estado compartido deben ser lock-free");
// Alineación explícita del estado compartido: los atómicos lock-free de ARM64
// exigen alineación natural; SHM_STATE_OFFSET (4096) la garantiza por página,
// y este assert blinda que el struct no introduzca padding de alineación
// incompatible entre compilaciones (daemon NDK vs app NDK).
static_assert(alignof(OmegaSharedState) <= 16,
              "OmegaSharedState no debe requerir alineacion > 16 (el mmap la garantiza)");

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
    /** write() con offset dentro de la zona de control (seqlock igual que write). */
    bool writeControl(size_t offset, const void* src, size_t len) noexcept;

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
