// IVANNA OMEGA SUPREME — Verificación de integridad del efecto stageado.
// CONFIDENCIAL (interno). Ver effect_integrity.h y docs/SECURITY_INTERNAL.md.
#include "effect_integrity.h"

#include "security/ivanna_integrity.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace ivanna::security {
namespace {

// Lee un archivo de texto pequeño (el hash esperado) y lo deja sin
// espacios/saltos de línea. Retorna false si no se puede leer.
bool read_text_file_trimmed(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char buf[256];
    const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    // Trim de espacios, \r y \n en ambos extremos.
    std::size_t start = 0;
    while (start < n && (buf[start] == ' ' || buf[start] == '\t' ||
                         buf[start] == '\r' || buf[start] == '\n')) {
        ++start;
    }
    std::size_t end = n;
    while (end > start && (buf[end - 1] == ' ' || buf[end - 1] == '\t' ||
                           buf[end - 1] == '\r' || buf[end - 1] == '\n')) {
        --end;
    }
    out.assign(buf + start, end - start);
    return true;
}

// Comparación en tiempo constante (el hash esperado no es secreto, pero el
// coste es el mismo y evita early-exit por timing).
bool ct_equal_hex(const char* a, const char* b, std::size_t n) {
    if (!a || !b) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool is_valid_sha256_hex(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return false;
    }
    return true;
}

constexpr int kEffectIntegrityUncomputed = -1;
std::atomic<int> g_effect_integrity_cached{kEffectIntegrityUncomputed};

void write_status_file(EffectIntegrity result) {
    FILE* f = std::fopen(kEffectIntegrityStatusPath, "w");
    if (!f) return;
    std::fprintf(f, "%s\n", effect_integrity_status_string(result));
    std::fclose(f);
}

}  // namespace

const char* effect_integrity_status_string(EffectIntegrity result) {
    switch (result) {
        case EffectIntegrity::kOk: return "ok";
        case EffectIntegrity::kMismatch: return "mismatch";
        case EffectIntegrity::kNoHashFile: return "no_hash_file";
        case EffectIntegrity::kNoEffectFile: return "no_effect_file";
        case EffectIntegrity::kIoError: return "io_error";
    }
    return "io_error";
}

EffectIntegrity effect_integrity_verify() {
    const int cached = g_effect_integrity_cached.load(std::memory_order_acquire);
    if (cached != kEffectIntegrityUncomputed) {
        return static_cast<EffectIntegrity>(cached);
    }

    EffectIntegrity result = EffectIntegrity::kIoError;

    // 1. Hash esperado (generado por CI en el módulo).
    std::string expected;
    if (!read_text_file_trimmed(kEffectHashPath, expected)) {
        result = EffectIntegrity::kNoHashFile;
        write_status_file(result);
        g_effect_integrity_cached.store(static_cast<int>(result),
                                        std::memory_order_release);
        return result;
    }
    if (!is_valid_sha256_hex(expected)) {
        // Archivo de hash corrupto o manipulado -> no se puede confiar.
        result = EffectIntegrity::kMismatch;
        write_status_file(result);
        g_effect_integrity_cached.store(static_cast<int>(result),
                                        std::memory_order_release);
        return result;
    }

    // 2. Hash real de la .so stageada.
    char actual_hex[65] = {};
    FILE* probe = std::fopen(kEffectSoPath, "rb");
    if (!probe) {
        result = EffectIntegrity::kNoEffectFile;
        write_status_file(result);
        g_effect_integrity_cached.store(static_cast<int>(result),
                                        std::memory_order_release);
        return result;
    }
    std::fclose(probe);
    if (!integrity_sha256_file(kEffectSoPath, actual_hex)) {
        result = EffectIntegrity::kIoError;
        write_status_file(result);
        g_effect_integrity_cached.store(static_cast<int>(result),
                                        std::memory_order_release);
        return result;
    }

    // 3. Comparar.
    result = ct_equal_hex(actual_hex, expected.c_str(), 64)
                 ? EffectIntegrity::kOk
                 : EffectIntegrity::kMismatch;
    write_status_file(result);
    g_effect_integrity_cached.store(static_cast<int>(result),
                                    std::memory_order_release);
    return result;
}

bool effect_integrity_ok() {
    const EffectIntegrity result = effect_integrity_verify();
    return result == EffectIntegrity::kOk ||
           result == EffectIntegrity::kNoHashFile;
}

}  // namespace ivanna::security
