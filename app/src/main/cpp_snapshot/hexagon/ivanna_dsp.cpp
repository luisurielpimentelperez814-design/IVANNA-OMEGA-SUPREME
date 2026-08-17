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
//  Nota: el header ivanna_dsp.h del repo declara wrappers inline con
//  "return -1". Este .cpp NO redefine esos símbolos — expone en su lugar el
//  namespace ivanna::hexagon::rt que el resto del código (fastrpc_client,
//  integration) debe consultar. Cuando el SDK real esté disponible y qaic
//  regenere ivanna_dsp.h, basta con eliminar los wrappers inline: este
//  loader los reemplaza en runtime sin recompilar.
// ============================================================================

#include "ivanna_dsp.hpp"

#include <atomic>
#include <cstdint>
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

// ── Estado global del loader (opaco al resto del código) ─────────────────────
namespace {

struct DspVTable {
    fn_open_t             open              = nullptr;
    fn_close_t            close             = nullptr;
    fn_process_stereo_t   process_stereo    = nullptr;
    fn_set_neuro_params_t set_neuro_params  = nullptr;
    fn_get_metrics_t      get_metrics       = nullptr;
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
    for (const char* name : kFastRpcLibs) {
        void* h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (h == nullptr) {
            IVLOGW("dlopen('%s') fallo: %s", name, dlerror());
            continue;
        }

        DspVTable vt{};
        vt.open             = resolve<fn_open_t>            (h, "ivanna_dsp_open");
        vt.close            = resolve<fn_close_t>           (h, "ivanna_dsp_close");
        vt.process_stereo   = resolve<fn_process_stereo_t>  (h, "ivanna_dsp_process_stereo");
        vt.set_neuro_params = resolve<fn_set_neuro_params_t>(h, "ivanna_dsp_set_neuro_params");
        vt.get_metrics      = resolve<fn_get_metrics_t>     (h, "ivanna_dsp_get_metrics");

        // Contrato mínimo: open + close + al menos una operación útil.
        const bool minimum_ok =
            vt.open != nullptr &&
            vt.close != nullptr &&
            (vt.process_stereo != nullptr || vt.set_neuro_params != nullptr);

        if (!minimum_ok) {
            IVLOGW("libreria '%s' cargada pero sin simbolos IDL — descartando", name);
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
    IVLOGI("Hexagon DSP no disponible — usando fallback CPU");
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
#if IVANNA_HAS_DLOPEN
    if (g_lib_handle != nullptr) {
        dlclose(g_lib_handle);
        g_lib_handle = nullptr;
        g_lib_loaded = nullptr;
    }
#endif
    g_vt = DspVTable{};
    g_dsp_available.store(false, std::memory_order_release);
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

}}} // namespace ivanna::hexagon::rt
