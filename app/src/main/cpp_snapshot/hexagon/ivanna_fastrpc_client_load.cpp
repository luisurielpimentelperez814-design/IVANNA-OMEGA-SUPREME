// ============================================================================
// ivanna_fastrpc_client_load.cpp — Fase H
// © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
//
// Objetivo: reemplazar el `load_fastrpc_symbols()` no-op de
// ivanna_fastrpc_client.cpp por una carga real basada en dlopen/dlsym.
//
// Se compila JUNTO A ivanna_fastrpc_client.cpp. Los símbolos g_dsp_open,
// g_dsp_close, etc., son static en ese TU, por lo que este archivo define
// unas funciones auxiliares en namespace `ivanna::dsp::phaseh` y expone
// un hook `phaseh_bind_symbols(...)` que se invoca desde el constructor de
// IvannaFastRpcClient (ver PATCH en README_INTEGRACION.md).
//
// Para NO tocar el .cpp existente (evita conflictos de merge), se aplica
// el patrón "linker hook": el .cpp existente ya declara los g_dsp_* como
// namespace-file-static, así que este archivo NO puede escribirlos
// directamente. Lo que sí hace es proveer una API pública que devuelve
// los mismos punteros resueltos vía dlsym; el .cpp original se modifica
// mínimamente para leerlos desde aquí (una línea en load_fastrpc_symbols).
//
// Ver README_INTEGRACION.md sección "Parche mínimo".
// ============================================================================

#include <atomic>
#include <cstdint>
#include <mutex>

#if defined(__ANDROID__) || defined(__linux__)
  #include <dlfcn.h>
  #define IVFRPC_HAS_DLOPEN 1
#else
  #define IVFRPC_HAS_DLOPEN 0
#endif

#ifdef __ANDROID__
  #include <android/log.h>
  #define IVFRPC_LOG(...) __android_log_print(ANDROID_LOG_INFO, "IvannaFastRPC", __VA_ARGS__)
#else
  #define IVFRPC_LOG(...) do {} while (0)
#endif

namespace ivanna { namespace dsp { namespace phaseh {

struct ResolvedSymbols {
    void* dsp_open          = nullptr;
    void* dsp_close         = nullptr;
    void* dsp_hrtf_init     = nullptr;
    void* dsp_hrtf_convolve = nullptr;
    void* dsp_fir_init      = nullptr;
    void* dsp_fir_upsample  = nullptr;
    const char* lib_name    = "";
    bool        loaded      = false;
};

static ResolvedSymbols g_syms{};
static std::once_flag  g_once;

static void do_resolve() {
#if IVFRPC_HAS_DLOPEN
    static constexpr const char* kLibs[] = { "libcdsprpc.so", "libadsprpc.so" };
    for (const char* name : kLibs) {
        void* h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            IVFRPC_LOG("dlopen('%s') fallo: %s", name, dlerror());
            continue;
        }
        g_syms.dsp_open          = dlsym(h, "ivanna_dsp_open");
        g_syms.dsp_close         = dlsym(h, "ivanna_dsp_close");
        g_syms.dsp_hrtf_init     = dlsym(h, "ivanna_dsp_hrtf_init");
        g_syms.dsp_hrtf_convolve = dlsym(h, "ivanna_dsp_hrtf_convolve");
        g_syms.dsp_fir_init      = dlsym(h, "ivanna_dsp_fir_init");
        g_syms.dsp_fir_upsample  = dlsym(h, "ivanna_dsp_fir_upsample");

        if (g_syms.dsp_open && g_syms.dsp_close) {
            g_syms.lib_name = name;
            g_syms.loaded   = true;
            IVFRPC_LOG("FastRPC listo via '%s'", name);
            return;
        }
        // Símbolos incompletos: descartar librería.
        dlclose(h);
        g_syms = ResolvedSymbols{};
    }
    IVFRPC_LOG("FastRPC no disponible — CPU fallback");
#endif
}

const ResolvedSymbols& resolved_symbols() noexcept {
    std::call_once(g_once, do_resolve);
    return g_syms;
}

bool is_loaded() noexcept { return resolved_symbols().loaded; }

}}} // namespace ivanna::dsp::phaseh
