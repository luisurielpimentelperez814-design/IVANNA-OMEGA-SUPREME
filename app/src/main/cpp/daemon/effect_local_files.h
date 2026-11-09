// effect_local_files.h — pre-creación de los archivos locales de Ruta B.
//
// Causa raíz (Moto G85, 2026-11-09): libomega_effect.so corre en audioserver/HAL
// con UID no-root y NO PUEDE crear archivos en /data/local/tmp/ (0771 root:shell,
// cero reglas SELinux para crearlo). El archivo de control
// /data/local/tmp/omega_effect_ctrl_local (una página, ver
// OmegaControlBus::REGION_SIZE) y el beacon /data/local/tmp/omega_effect_beacon
// (OMEGA_EFFECT_BEACON_SIZE) los crea ÚNICAMENTE el efecto al
// inicializarse/habilitarse. Como no puede crearlos, el demonio lee un beacon
// inexistente (consumer=null, effect_state=-1, dsp_frames=0) y la app busca el
// archivo de control cada 1 s sin encontrarlo.
//
// El demonio (root) pre-crea ambos archivos con 0666 en cada arranque: con el
// archivo ya existente, el open(O_CREAT) del efecto ya no necesita permiso de
// creación del directorio. NO se escribe magic ni snapshot aquí: solo existencia
// + permisos + tamaño. El efecto los inicializa (magic, contadores) al mapearlos.
//
// Header-only y sin dependencias IVANNA para poder probarlo en host sin NDK
// (ver tests/test_effect_local_files.cpp).
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

namespace ivanna {

// Resultado de pre-crear un archivo. `ok == false` no es fatal: el llamador
// loguea el motivo y sigue (el efecto puede reintentarlo en su propio open()).
struct LocalFilePrecreateResult {
    bool ok = false;
    std::string detail;
};

// Garantiza que `path` exista con modo 0666 y tamaño `size`:
//   - open(O_RDWR|O_CREAT, 0666) + fchmod(0666) (el modo de open lo recorta
//     umask; el fchmod lo fuerza: audioserver necesita escritura para abrir
//     el beacon/control con O_RDWR).
//   - ftruncate(fd, size) SOLO si el tamaño actual es 0: nunca se trunca un
//     archivo que el efecto ya está usando (tamaño correcto), ni uno parcial
//     de origen desconocido (se reporta como advertencia, sin tocar).
// No escribe contenido: el efecto inicializa el magic al mapearlo.
inline LocalFilePrecreateResult precreateLocalFile(const char* path, std::size_t size) noexcept {
    LocalFilePrecreateResult r;
    if (path == nullptr || path[0] == '\0' || size == 0) {
        r.detail = "ruta/tamaño inválido";
        return r;
    }
    const int fd = ::open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) {
        const int e = errno;
        r.detail = std::string("open failed: ") + std::strerror(e);
        return r;
    }
    if (::fchmod(fd, 0666) < 0) {
        const int e = errno;
        ::close(fd);
        r.detail = std::string("fchmod 0666 failed: ") + std::strerror(e);
        return r;
    }
    struct stat st {};
    if (::fstat(fd, &st) < 0) {
        const int e = errno;
        ::close(fd);
        r.detail = std::string("fstat failed: ") + std::strerror(e);
        return r;
    }
    if (st.st_size == 0) {
        if (::ftruncate(fd, static_cast<off_t>(size)) < 0) {
            const int e = errno;
            ::close(fd);
            r.detail = std::string("ftruncate failed: ") + std::strerror(e);
            return r;
        }
        r.detail = "creado con tamaño " + std::to_string(size);
    } else if (static_cast<std::size_t>(st.st_size) == size) {
        r.detail = "ya existía con tamaño correcto, sin tocar";
    } else {
        ::close(fd);
        r.detail = "tamaño inesperado " + std::to_string(st.st_size) + " (esperado " +
                   std::to_string(size) + "); no se trunca";
        return r;
    }
    ::close(fd);
    r.ok = true;
    return r;
}

}  // namespace ivanna
