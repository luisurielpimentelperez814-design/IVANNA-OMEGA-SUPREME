// test_effect_local_files.cpp -- barrera de regresión de la pre-creación de
// archivos locales de Ruta B (FIX Moto G85 2026-11-09).
//
// Causa raíz que cierra: libomega_effect.so corre en audioserver/HAL con UID
// no-root y NO PUEDE crear archivos en /data/local/tmp/ (0771 root:shell, cero
// reglas SELinux). El demonio (root) debe pre-crear con 0666 el archivo de
// control y el beacon en cada arranque. Este test fija el contrato de
// ivanna::precreateLocalFile() (daemon/effect_local_files.h):
//   1. Archivo inexistente => se crea con modo 0666 y tamaño exacto.
//   2. Archivo existente con tamaño correcto y contenido => NO se toca
//      (ni truncado ni reescrito): el efecto puede estar usándolo.
//   3. Archivo existente vacío (tamaño 0) => se dimensiona al tamaño pedido.
//   4. Archivo existente con tamaño parcial distinto de 0 => NO se trunca y
//      se reporta fallo (estado corrupto visible, no silencioso).
//   5. Argumentos inválidos (ruta nula/vacía, tamaño 0) => fallo.
//   6. Idempotencia: dos llamadas seguidas dejan el archivo intacto.
// Compila en host sin NDK ni root.
#include "daemon/effect_local_files.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace ivanna;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else         { std::printf("ok:   %s\n", msg); } \
} while (0)

static std::string tmpPath(const char* tag) {
    const char* tmp = std::getenv("TMPDIR");
    return std::string(tmp && *tmp ? tmp : "/tmp") + "/omega_local_file_test_" + tag +
           "_" + std::to_string(::getpid());
}

static bool readAll(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[8192];
    size_t n = 0;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

int main() {
    // 1. Inexistente => creado con 0666 y tamaño exacto (contenido a ceros).
    {
        const std::string p = tmpPath("missing");
        ::unlink(p.c_str());
        const LocalFilePrecreateResult r = precreateLocalFile(p.c_str(), 4096);
        CHECK(r.ok, "inexistente -> ok");
        struct stat st {};
        CHECK(::stat(p.c_str(), &st) == 0 && st.st_size == 4096, "inexistente -> tamaño 4096");
        CHECK(::stat(p.c_str(), &st) == 0 && (st.st_mode & 0777) == 0666, "inexistente -> modo 0666");
        std::string content;
        CHECK(readAll(p, content) && content.size() == 4096 &&
                  content.find_first_not_of('\0') == std::string::npos,
              "inexistente -> contenido a ceros");
        ::unlink(p.c_str());
    }

    // 2. Existente con tamaño correcto y contenido => NO se toca.
    {
        const std::string p = tmpPath("intact");
        ::unlink(p.c_str());
        {
            FILE* f = std::fopen(p.c_str(), "wb");
            const std::string marker(72, 'X');
            std::fwrite(marker.data(), 1, marker.size(), f);
            std::fclose(f);
        }
        const LocalFilePrecreateResult r = precreateLocalFile(p.c_str(), 72);
        CHECK(r.ok, "existente correcto -> ok");
        std::string content;
        CHECK(readAll(p, content) && content == std::string(72, 'X'),
              "existente correcto -> contenido intacto (no truncado)");
        struct stat st {};
        CHECK(::stat(p.c_str(), &st) == 0 && st.st_size == 72, "existente correcto -> tamaño intacto");
        ::unlink(p.c_str());
    }

    // 3. Existente vacío (tamaño 0) => se dimensiona.
    {
        const std::string p = tmpPath("empty");
        ::unlink(p.c_str());
        FILE* f = std::fopen(p.c_str(), "wb");
        std::fclose(f);
        const LocalFilePrecreateResult r = precreateLocalFile(p.c_str(), 72);
        CHECK(r.ok, "vacío -> ok");
        struct stat st {};
        CHECK(::stat(p.c_str(), &st) == 0 && st.st_size == 72, "vacío -> dimensionado a 72");
        ::unlink(p.c_str());
    }

    // 4. Tamaño parcial distinto de 0 => NO se trunca, se reporta fallo.
    {
        const std::string p = tmpPath("partial");
        ::unlink(p.c_str());
        {
            FILE* f = std::fopen(p.c_str(), "wb");
            const std::string marker(10, 'Y');
            std::fwrite(marker.data(), 1, marker.size(), f);
            std::fclose(f);
        }
        const LocalFilePrecreateResult r = precreateLocalFile(p.c_str(), 72);
        CHECK(!r.ok, "parcial (10 B) -> fallo reportado");
        CHECK(!r.detail.empty(), "parcial -> detalle no vacío");
        struct stat st {};
        CHECK(::stat(p.c_str(), &st) == 0 && st.st_size == 10, "parcial -> tamaño intacto (no truncado)");
        std::string content;
        CHECK(readAll(p, content) && content == std::string(10, 'Y'), "parcial -> contenido intacto");
        ::unlink(p.c_str());
    }

    // 5. Argumentos inválidos.
    {
        CHECK(!precreateLocalFile(nullptr, 72).ok, "ruta nula -> fallo");
        CHECK(!precreateLocalFile("", 72).ok, "ruta vacía -> fallo");
        const std::string p = tmpPath("zero");
        ::unlink(p.c_str());
        CHECK(!precreateLocalFile(p.c_str(), 0).ok, "tamaño 0 -> fallo");
        struct stat st {};
        CHECK(::stat(p.c_str(), &st) != 0, "tamaño 0 -> archivo no creado");
    }

    // 6. Idempotencia: dos llamadas seguidas, contenido intacto.
    {
        const std::string p = tmpPath("idem");
        ::unlink(p.c_str());
        CHECK(precreateLocalFile(p.c_str(), 4096).ok, "idempotencia: primera ok");
        {
            FILE* f = std::fopen(p.c_str(), "r+b");
            const std::string marker(8, 'Z');
            std::fwrite(marker.data(), 1, marker.size(), f);
            std::fclose(f);
        }
        const LocalFilePrecreateResult r2 = precreateLocalFile(p.c_str(), 4096);
        CHECK(r2.ok, "idempotencia: segunda ok");
        std::string content;
        CHECK(readAll(p, content) && content.size() == 4096 &&
                  content.compare(0, 8, std::string(8, 'Z')) == 0,
              "idempotencia: contenido intacto tras segunda llamada");
        ::unlink(p.c_str());
    }

    std::printf(failures ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", failures);
    return failures ? 1 : 0;
}
