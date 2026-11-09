// IVANNA OMEGA SUPREME — Anti-debug / anti-tamper / anti-hook.
// CONFIDENCIAL (interno). Ver ivanna_antitamper.h y docs/SECURITY_INTERNAL.md.
//
// Los literales sensibles (nombres de frameworks, rutas de su) van cifrados
// con IVANNA_XSTR (ver ivanna_obfuscate.h): no aparecen en claro en .rodata.
// El evaluador usa control-flow flattening a nivel de fuente (despachador
// switch con estados no secuenciales) + código señuelo con predicados
// opacos. El resultado es idéntico a una secuencia lineal (OR de señales);
// el CFG decompilado es un único switch, no una lista de ifs.
#include "ivanna_antitamper.h"

#include "ivanna_obfuscate.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace ivanna::security {
namespace {

using obf::XorString;

// ── Helpers de strings cifrados ─────────────────────────────────────────────
// Descifra `xstr` en un buffer de pila, comprueba si `text` lo contiene
// (case-insensitive) y borra el buffer. El literal nunca queda en claro
// fuera del buffer de pila.
template <std::size_t N>
bool contains_encrypted(const char* text, const XorString<N>& xstr) {
    char buf[64];
    if (N > sizeof(buf)) return false;
    if (!xstr.decrypt(buf, sizeof(buf))) return false;
    const bool hit = antitamper_contains_ci(text, buf);
    obf::wipe(buf, sizeof(buf));
    return hit;
}

// Descifra `xstr` en un buffer de pila y comprueba si el archivo existe.
template <std::size_t N>
bool file_exists_encrypted(const XorString<N>& xstr) {
    char buf[64];
    if (N > sizeof(buf)) return false;
    if (!xstr.decrypt(buf, sizeof(buf))) return false;
    const bool exists = (access(buf, F_OK) == 0);
    obf::wipe(buf, sizeof(buf));
    return exists;
}

// ── Comprobaciones individuales ─────────────────────────────────────────────

// Depurador: TracerPid de /proc/self/status. != 0 -> un depurador está
// attached por ptrace. Lectura pasiva (sin efectos); funciona en host Linux
// también (devuelve false si no hay trazador).
bool check_debugger() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return false;
    char content[8192];
    const std::size_t n = std::fread(content, 1, sizeof(content) - 1, f);
    std::fclose(f);
    content[n] = '\0';
    const int tracer = antitamper_parse_tracer_pid(content);
    return tracer > 0;
}

// Mapa de memoria: busca artefactos de Frida y de frameworks de hooks en
// /proc/self/maps. Si el archivo no se puede leer, no se marca nada (no se
// bloquea por un fallo de E/S; las demás comprobaciones siguen).
std::uint32_t scan_process_maps() {
    std::uint32_t found = 0;
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[4096];
    while (std::fgets(line, sizeof(line), f)) {
        found |= antitamper_scan_maps_text(line);
        if (found == (kAntiTamperFrida | kAntiTamperHookFramework)) break;
    }
    std::fclose(f);
    return found;
}

// Archivos conocidos del servidor Frida (solo Android; en host no existen).
bool check_frida_files() {
#if defined(__ANDROID__)
    if (file_exists_encrypted(IVANNA_XSTR("/data/local/tmp/frida-server"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/data/local/tmp/re.frida.server"))) return true;
    return false;
#else
    return false;
#endif
}

// Emulador: propiedades del sistema + /dev/qemu_pipe (solo Android).
bool check_emulator() {
#if defined(__ANDROID__)
    const char* props[] = {"ro.hardware", "ro.product.model",
                           "ro.build.fingerprint", "ro.product.device"};
    char value[PROP_VALUE_MAX];
    for (const char* prop : props) {
        if (__system_property_get(prop, value) > 0) {
            if (antitamper_is_emulator_marker(value)) return true;
        }
    }
    char qemu_path[32];
    if (IVANNA_XSTR("/dev/qemu_pipe").decrypt(qemu_path, sizeof(qemu_path))) {
        const bool exists = (access(qemu_path, F_OK) == 0);
        obf::wipe(qemu_path, sizeof(qemu_path));
        if (exists) return true;
    }
    return false;
#else
    return false;
#endif
}

// Root: binarios su y archivos de Magisk/Superuser (solo Android).
// SOLO señal informativa (ver ivanna_antitamper.h): no dispara lockdown.
bool check_root() {
#if defined(__ANDROID__)
    if (file_exists_encrypted(IVANNA_XSTR("/system/bin/su"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/system/xbin/su"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/sbin/su"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/system/app/Superuser.apk"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/data/adb/magisk"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/sbin/.magisk"))) return true;
    if (file_exists_encrypted(IVANNA_XSTR("/data/local/tmp/su"))) return true;
    return false;
#else
    return false;
#endif
}

// Caché del resultado: kUncomputed = sin calcular.
constexpr std::uint32_t kAntiTamperUncomputed = 0xFFFFFFFFu;
std::atomic<std::uint32_t> g_antitamper_cached{kAntiTamperUncomputed};

}  // namespace

// ── Helpers testeables en host ──────────────────────────────────────────────

int antitamper_parse_tracer_pid(const char* status_text) {
    if (!status_text) return -1;
    // El nombre del campo va cifrado (no aparece en claro en .rodata).
    char field[16];
    if (!IVANNA_XSTR("TracerPid:").decrypt(field, sizeof(field))) return -1;
    const char* p = std::strstr(status_text, field);
    obf::wipe(field, sizeof(field));
    if (!p) return -1;
    p += 10;  // strlen("TracerPid:")
    while (*p == ' ' || *p == '\t') ++p;
    if (*p < '0' || *p > '9') return -1;
    return std::atoi(p);
}

bool antitamper_contains_ci(const char* haystack, const char* needle) {
    if (!haystack || !needle || needle[0] == '\0') return false;
    const std::size_t nlen = std::strlen(needle);
    const std::size_t hlen = std::strlen(haystack);
    if (nlen > hlen) return false;
    for (std::size_t i = 0; i + nlen <= hlen; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < nlen; ++j) {
            const char a = static_cast<char>(
                std::tolower(static_cast<unsigned char>(haystack[i + j])));
            const char b = static_cast<char>(
                std::tolower(static_cast<unsigned char>(needle[j])));
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool antitamper_is_emulator_marker(const char* value) {
    if (!value || value[0] == '\0') return false;
    // Marcadores cifrados (no aparecen en claro en .rodata).
    if (contains_encrypted(value, IVANNA_XSTR("goldfish"))) return true;
    if (contains_encrypted(value, IVANNA_XSTR("ranchu"))) return true;
    if (contains_encrypted(value, IVANNA_XSTR("generic"))) return true;
    if (contains_encrypted(value, IVANNA_XSTR("sdk"))) return true;
    if (contains_encrypted(value, IVANNA_XSTR("emulator"))) return true;
    if (contains_encrypted(value, IVANNA_XSTR("vbox86"))) return true;
    if (contains_encrypted(value, IVANNA_XSTR("qemu"))) return true;
    return false;
}

std::uint32_t antitamper_scan_maps_text(const char* text) {
    if (!text) return 0;
    std::uint32_t found = 0;
    // Frida (agente o gadget).
    if (contains_encrypted(text, IVANNA_XSTR("frida"))) found |= kAntiTamperFrida;
    if (contains_encrypted(text, IVANNA_XSTR("gadget"))) found |= kAntiTamperFrida;
    // Frameworks de hooks. NO se marcan Zygisk ni Riru (son de Magisk; el
    // producto está diseñado para dispositivos con Magisk).
    if (contains_encrypted(text, IVANNA_XSTR("xposed"))) found |= kAntiTamperHookFramework;
    if (contains_encrypted(text, IVANNA_XSTR("lsposed"))) found |= kAntiTamperHookFramework;
    if (contains_encrypted(text, IVANNA_XSTR("edxp"))) found |= kAntiTamperHookFramework;
    if (contains_encrypted(text, IVANNA_XSTR("substrate"))) found |= kAntiTamperHookFramework;
    if (contains_encrypted(text, IVANNA_XSTR("cydia"))) found |= kAntiTamperHookFramework;
    return found;
}

// ── Evaluador (control-flow flattening) ─────────────────────────────────────

std::uint32_t antitamper_evaluate() {
    std::uint32_t cached = g_antitamper_cached.load(std::memory_order_acquire);
    if (cached != kAntiTamperUncomputed) return cached;

    std::uint32_t found = 0;
    // Las comprobaciones no corren en orden lineal: un despachador switch
    // con estados no secuenciales las ejecuta en orden barajado. El CFG
    // decompilado es un único switch con transiciones no obvias, en vez de
    // una secuencia lineal de ifs. El resultado es idéntico (OR de señales).
    int state = 0x2A;  // entrada
    for (;;) {
        switch (state) {
            case 0x2A:  // depurador (TracerPid)
                if (check_debugger()) found |= kAntiTamperDebugger;
                state = 0x17;
                break;
            case 0x17:  // mapa de memoria (Frida / hooks) + archivos Frida
                found |= scan_process_maps();
                if (check_frida_files()) found |= kAntiTamperFrida;
                state = 0x5E;
                break;
            case 0x5E:  // emulador
                if (check_emulator()) found |= kAntiTamperEmulator;
                state = 0x0B;
                break;
            case 0x0B:  // root (solo señal informativa)
                if (check_root()) found |= kAntiTamperRoot;
                state = 0x63;
                break;
            case 0x63:  // salida
            default:
                goto antitamper_done;
        }
    }
antitamper_done:

    // ── Bogus control flow (señuelo) ────────────────────────────────────────
    // Rama guardada por un predicado opaco (siempre verdadera): calcula
    // valores plausibles que se descartan. Sin efectos (no log, no estado);
    // solo existe para que el flujo decompilado tenga ramas falsas.
    if (obf::opaque_always_true(static_cast<int>(found))) {
        volatile std::uint32_t decoy = 0;
        for (int i = 0; i < 16; ++i) {
            decoy = decoy * 31u + static_cast<std::uint32_t>(i);
        }
        (void)decoy;
    }
    // Señuelo de "bypass": nunca se ejecuta (predicado siempre falso), pero
    // parece una vía para desactivar la comprobación.
    if (obf::opaque_always_false(static_cast<int>(found))) {
        found = 0;
    }

    g_antitamper_cached.store(found, std::memory_order_release);
    return found;
}

bool antitamper_should_lockdown() {
    return (antitamper_evaluate() & kAntiTamperLockdownMask) != 0;
}

void antitamper_reset_for_tests() {
    g_antitamper_cached.store(kAntiTamperUncomputed, std::memory_order_release);
}

}  // namespace ivanna::security
