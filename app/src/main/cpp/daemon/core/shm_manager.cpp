// daemon/core/shm_manager.cpp
// Implementación de OmegaShmManager para el proceso daemon.
//
// Responsabilidades de este módulo vs ivanna_daemon.cpp:
//   · ivanna_daemon.cpp — crea el archivo SHM en initialize_shared_memory()
//     y lo expone vía SCM_RIGHTS a la app. Sigue siendo correcto y no se
//     modifica: ese path existía antes de este módulo.
//   · shm_manager.cpp — añade una API de alto nivel reutilizable para
//     escritura seqlock que el servidor de comandos (command_server.cpp)
//     y futuros módulos del daemon pueden usar sin duplicar el boilerplate
//     de mmap/mlock/seqlock.
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
#include <android/log.h>

#define SHM_TAG "IVANNA_SHM"
#define SHM_LOG(fmt, ...)     __android_log_print(ANDROID_LOG_INFO, SHM_TAG, fmt, ##__VA_ARGS__)

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
    if (!m_base || !src) return false;

    constexpr size_t kHeaderSize = sizeof(ShmHeader);
    if (len > SHM_SIZE - kHeaderSize) return false;

    auto* hdr  = static_cast<ShmHeader*>(m_base);
    auto* data = static_cast<uint8_t*>(m_base) + kHeaderSize;

    // Seqlock write protocol:
    //   1. epoch → impar (comenzando escritura)
    //   2. __sync_synchronize() — barrier completo
    //   3. copiar datos
    //   4. actualizar frame_len
    //   5. epoch → par (escritura completa)
    //   Lectores que leen epoch impar o ven cambio entre pre/post repiten.
    const uint64_t seq = hdr->epoch.load(std::memory_order_relaxed);
    hdr->epoch.store(seq | 1ULL, std::memory_order_release);  // impar
    __sync_synchronize();

    std::memcpy(data, src, len);
    hdr->frame_len = static_cast<uint32_t>(len);

    __sync_synchronize();
    hdr->epoch.store(seq + 2ULL, std::memory_order_release);  // par (+2)

    return true;
}

} // namespace ivanna
