// ============================================================================
//  ivanna_dsp.cpp — Runtime FastRPC loader (dlopen/dlsym) + CPU fallback bridge
//  © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
//  Fase H: implementación real del "stub" generado por qaic.
//
//  Comportamiento:
//    1. Intenta cargar dinámicamente libcdsprpc.so (cDSP) y como respaldo
//       libadsprpc.so (aDSP) usando dlopen(..., RTLD_NOW | RTLD_LOCAL).
//    2. Resuelve las 5 funciones IDL exportadas por el stub qaic real:
//         ivanna_dsp_open, ivanna_dsp_close, ivanna_dsp_process_stereo,
//         ivanna_dsp_set_neuro_params, ivanna_dsp_get_metrics
//    3. Si cualquier símbolo no está presente marca g_dsp_available=false
//       y todas las funciones retornan -ENOSYS de forma segura para que el
//       llamador conmute a la ruta CPU (FIRUpsamplerEngine).
//    4. Es thread-safe: la carga se hace una única vez con std::call_once.
//    5. Nunca lanza excepciones; nunca aborta si el DSP no está disponible.
//
//  Nota: la API de bajo nivel (namespace ivanna::hexagon::rt) está declarada
//  en ivanna_dsp_rt.hpp, que es la fuente de verdad de este loader. La
//  fachada pública de alto nivel (namespace ivanna::hexagon) declarada en
//  hexagon_dsp_integration.hpp se implementa al final de este archivo y
//  delega en rt:: — así el símbolo ensure_available() que consume
//  npe_engine queda definido (antes era un símbolo indefinido: crash/break).
// ============================================================================

#include "ivanna_dsp_rt.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(__ANDROID__) || defined(__linux__)
  #include <dlfcn.h>
  #define IVANNA_HAS_DLOPEN 1
#else
  #define IVANNA_HAS_DLOPEN 0
#endif

#ifdef __ANDROID__
  #include <android/log.h>
  #define IVLOG_TAG "IvannaDSP"
  #define IVLOGI(...) __android_log_print(ANDROID_LOG_INFO,  IVLOG_TAG, __VA_ARGS__)
  #define IVLOGW(...) __android_log_print(ANDROID_LOG_WARN,  IVLOG_TAG, __VA_ARGS__)
  #define IVLOGE(...) __android_log_print(ANDROID_LOG_ERROR, IVLOG_TAG, __VA_ARGS__)
#else
  #define IVLOGI(...) do {} while (0)
  #define IVLOGW(...) do {} while (0)
  #define IVLOGE(...) do {} while (0)
#endif

namespace ivanna { namespace hexagon { namespace rt {

// ── Firmas IDL (equivalentes a las declaradas en ivanna_dsp.h) ───────────────
using ivanna_dsp_handle_t = void*;

using fn_open_t              = int (*)(ivanna_dsp_handle_t*);
using fn_close_t             = int (*)(ivanna_dsp_handle_t);
using fn_process_stereo_t    = int (*)(ivanna_dsp_handle_t,
                                       const float*, const float*,
                                       float*, float*, int);
using fn_set_neuro_params_t  = int (*)(ivanna_dsp_handle_t,
                                       float, float, float, float);
using fn_get_metrics_t       = int (*)(ivanna_dsp_handle_t, float*, float*);

// Firmas HRTF/FIR idénticas a las declaradas en ivanna_dsp_rt.hpp (extern "C").
using dsp_hrtf_init_fn     = int (*)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
using dsp_hrtf_convolve_fn = int (*)(void*, const float*, int, const float*, int,
                                     float*, int, float*, int, float, float, uint32_t);
using dsp_fir_init_fn      = int (*)(void*, uint32_t, uint32_t);
using dsp_fir_upsample_fn  = int (*)(void*, const float*, int, float*, int, uint32_t);

// ── Estado global del loader (opaco al resto del código) ─────────────────────
namespace {

struct DspVTable {
    fn_open_t             open              = nullptr;
    fn_close_t            close             = nullptr;
    fn_process_stereo_t   process_stereo    = nullptr;
    fn_set_neuro_params_t set_neuro_params  = nullptr;
    fn_get_metrics_t      get_metrics       = nullptr;
    // Símbolos opcionales del cliente FastRPC de alto nivel.
    dsp_hrtf_init_fn      hrtf_init         = nullptr;
    dsp_hrtf_convolve_fn  hrtf_convolve     = nullptr;
    dsp_fir_init_fn       fir_init          = nullptr;
    dsp_fir_upsample_fn   fir_upsample      = nullptr;
};

std::once_flag           g_load_once;
std::atomic<bool>        g_dsp_available{false};
DspVTable                g_vt{};
void*                    g_lib_handle = nullptr;   // dlopen handle (cDSP o aDSP)
const char*              g_lib_loaded = nullptr;   // nombre de la librería activa

// Orden de búsqueda: cDSP primero (baja latencia, Q66xx/SM8xxx),
// luego aDSP como respaldo (chips más antiguos).
constexpr const char* kFastRpcLibs[] = {
    "libcdsprpc.so",
    "libadsprpc.so",
};

// FIX(diagnostico honesto): detecta si el SoC es Qualcomm leyendo el campo
// Hardware de /proc/cpuinfo. Si NO es Qualcomm, el Hexagon cDSP no puede
// existir y el loader evita spamear WARN por cada dlopen fallido — es
// esperado, no un fallo. Si es Qualcomm pero las libs no cargan, SÍ se
// advierte (el DSP debería estar).
static bool is_qualcomm_soc() {
#if IVANNA_HAS_DLOPEN
    FILE* f = std::fopen("/proc/cpuinfo", "r");
    if (!f) return true;  // sin info -> asumir posible (no silenciar warnings)
    char line[256];
    bool qc = false;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strstr(line, "Qualcomm") || std::strstr(line, "qcom") ||
            std::strstr(line, "SM") || std::strstr(line, "Snapdragon") ||
            std::strstr(line, "Hexagon")) {
            qc = true;
            break;
        }
    }
    std::fclose(f);
    return qc;
#else
    return true;
#endif
}

template <typename FnPtr>
static FnPtr resolve(void* lib, const char* sym) {
#if IVANNA_HAS_DLOPEN
    dlerror(); // limpia error previo
    void* p = dlsym(lib, sym);
    const char* err = dlerror();
    if (err != nullptr || p == nullptr) {
        IVLOGW("dlsym('%s') fallo: %s", sym, err ? err : "nullptr");
        return nullptr;
    }
    return reinterpret_cast<FnPtr>(p);
#else
    (void)lib; (void)sym; return nullptr;
#endif
}

static void load_once() {
#if IVANNA_HAS_DLOPEN
    const bool qc = is_qualcomm_soc();
    if (!qc) {
        IVLOGI("SoC no-Qualcomm detectado — Hexagon cDSP no existe aqui, fallback CPU");
    }
    for (const char* name : kFastRpcLibs) {
        void* h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (h == nullptr) {
            if (qc) IVLOGW("dlopen('%s') fallo: %s", name, dlerror());
            continue;
        }

        DspVTable vt{};
        vt.open             = resolve<fn_open_t>            (h, "ivanna_dsp_open");
        vt.close            = resolve<fn_close_t>           (h, "ivanna_dsp_close");
        vt.process_stereo   = resolve<fn_process_stereo_t>  (h, "ivanna_dsp_process_stereo");
        vt.set_neuro_params = resolve<fn_set_neuro_params_t>(h, "ivanna_dsp_set_neuro_params");
        vt.get_metrics      = resolve<fn_get_metrics_t>     (h, "ivanna_dsp_get_metrics");
        vt.hrtf_init        = resolve<dsp_hrtf_init_fn>     (h, "ivanna_dsp_hrtf_init");
        vt.hrtf_convolve    = resolve<dsp_hrtf_convolve_fn> (h, "ivanna_dsp_hrtf_convolve");
        vt.fir_init         = resolve<dsp_fir_init_fn>      (h, "ivanna_dsp_fir_init");
        vt.fir_upsample     = resolve<dsp_fir_upsample_fn>  (h, "ivanna_dsp_fir_upsample");

        // Contrato mínimo: open + close + al menos una operación útil.
        const bool minimum_ok =
            vt.open != nullptr &&
            vt.close != nullptr &&
            (vt.process_stereo != nullptr || vt.set_neuro_params != nullptr);

        if (!minimum_ok) {
            if (qc) IVLOGW("libreria '%s' cargada pero sin simbolos IDL — descartando", name);
            dlclose(h);
            continue;
        }

        g_lib_handle = h;
        g_lib_loaded = name;
        g_vt = vt;
        g_dsp_available.store(true, std::memory_order_release);
        IVLOGI("Hexagon DSP disponible via '%s' (open=%p process=%p)",
               name,
               reinterpret_cast<void*>(vt.open),
               reinterpret_cast<void*>(vt.process_stereo));
        return;
    }
    if (qc) IVLOGI("Hexagon DSP no disponible en SoC Qualcomm — usando fallback CPU");
#else
    IVLOGI("dlopen no soportado en esta plataforma — fallback CPU");
#endif
    g_dsp_available.store(false, std::memory_order_release);
}

} // namespace

// ── API pública consumida por fastrpc_client / npe_engine ────────────────────

bool ensure_loaded() noexcept {
    std::call_once(g_load_once, load_once);
    return g_dsp_available.load(std::memory_order_acquire);
}

bool is_available() noexcept {
    return g_dsp_available.load(std::memory_order_acquire);
}

const char* active_library() noexcept {
    return g_lib_loaded ? g_lib_loaded : "";
}

void release() noexcept {
    // FIX(carrera de liberacion): antes se hacia dlclose(g_lib_handle) con la
    // vtable aun poblada y g_dsp_available aun en true. Un hilo de audio que
    // entrara a dsp_process_stereo() en ese instante podia leer un puntero de
    // funcion valido y saltar a codigo de una libreria YA descargada
    // (use-after-free / salto a memoria liberada). Orden correcto:
    //   1) marcar no-disponible primero (los wrappers empiezan a devolver -1),
    //   2) invalidar la vtable (ningun puntero de funcion queda alcanzable),
    //   3) SOLO ENTONCES dlclose del handle.
    // No elimina toda ventana (un hilo ya DENTRO de una llamada al DSP no es
    // interrumpible sin mas sincronizacion), pero cierra la de entrada: ningun
    // hilo NUEVO puede resolver un puntero tras el paso 2.
    g_dsp_available.store(false, std::memory_order_release);
    g_vt = DspVTable{};
#if IVANNA_HAS_DLOPEN
    if (g_lib_handle != nullptr) {
        dlclose(g_lib_handle);
        g_lib_handle = nullptr;
        g_lib_loaded = nullptr;
    }
#endif
}

int dsp_open(void** out_handle) noexcept {
    if (!ensure_loaded() || g_vt.open == nullptr || out_handle == nullptr) return -1;
    return g_vt.open(out_handle);
}

int dsp_close(void* handle) noexcept {
    if (!g_dsp_available.load(std::memory_order_acquire) || g_vt.close == nullptr) return -1;
    return g_vt.close(handle);
}

int dsp_process_stereo(void* handle,
                       const float* in_l, const float* in_r,
                       float* out_l, float* out_r,
                       int frames) noexcept {
    if (!g_dsp_available.load(std::memory_order_acquire) ||
        g_vt.process_stereo == nullptr) return -1;
    if (handle == nullptr || in_l == nullptr || in_r == nullptr ||
        out_l == nullptr || out_r == nullptr || frames <= 0) return -1;
    return g_vt.process_stereo(handle, in_l, in_r, out_l, out_r, frames);
}

int dsp_set_neuro_params(void* handle,
                         float alpha, float beta, float gamma, float delta) noexcept {
    if (!g_dsp_available.load(std::memory_order_acquire) ||
        g_vt.set_neuro_params == nullptr) return -1;
    return g_vt.set_neuro_params(handle, alpha, beta, gamma, delta);
}

int dsp_get_metrics(void* handle, float* cpu_load, float* peak_amp) noexcept {
    if (!g_dsp_available.load(std::memory_order_acquire) ||
        g_vt.get_metrics == nullptr) return -1;
    if (cpu_load) *cpu_load = 0.0f;
    if (peak_amp) *peak_amp = 0.0f;
    return g_vt.get_metrics(handle, cpu_load, peak_amp);
}

// ── Símbolos IDL extendidos (cliente FastRPC) — fuerzan la carga y devuelven
// el puntero resuelto, o nullptr si el DSP / símbolo no está disponible.
dsp_hrtf_init_fn     dsp_hrtf_init_sym() noexcept     { return ensure_loaded() ? g_vt.hrtf_init     : nullptr; }
dsp_hrtf_convolve_fn dsp_hrtf_convolve_sym() noexcept { return ensure_loaded() ? g_vt.hrtf_convolve : nullptr; }
dsp_fir_init_fn      dsp_fir_init_sym() noexcept      { return ensure_loaded() ? g_vt.fir_init      : nullptr; }
dsp_fir_upsample_fn  dsp_fir_upsample_sym() noexcept  { return ensure_loaded() ? g_vt.fir_upsample  : nullptr; }

}}} // namespace ivanna::hexagon::rt

// ── Fachada pública de alto nivel (ivanna::hexagon) ─────────────────────────
// Implementa el contrato declarado en hexagon_dsp_integration.hpp delegando
// en el loader rt de arriba. Sin estas definiciones, ensure_available() era
// un símbolo declarado-pero-no-definido: cualquier TU que lo llamara (p.ej.
// npe_engine) fallaba al enlazar (build -z defs) o al primer uso (lazy
// binding). Ahora la cadena pública está cerrada.
namespace ivanna { namespace hexagon {

bool ensure_available() noexcept {
    return rt::ensure_loaded();
}

bool is_available() noexcept {
    return rt::is_available();
}

const char* active_library() noexcept {
    return rt::active_library();
}

void release() noexcept {
    rt::release();
}

}} // namespace ivanna::hexagon
