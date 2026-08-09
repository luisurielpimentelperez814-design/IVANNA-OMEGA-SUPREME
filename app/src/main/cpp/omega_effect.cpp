#include <jni.h>
#include <android/log.h>
#include "IvannaFusionCore.cpp"
#include <vector>
#include "audio_effect_compat.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define LOG_TAG "IvannaOmegaEffect"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
// FIX (trazabilidad HRTF): sin este canal no habia forma de saber, en un
// dispositivo real, si el motor estaba usando el dataset MEDIDO o habia
// caido al sintetico — el fallback era completamente silencioso.
#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#endif

static IvannaFusionCore* g_fusionCore = nullptr;

extern "C" {

// NOTA (audit fix): g_fusionCore se mantiene sólo para el canal legacy JNI
// (IvannaNativeLib.nativeInitDSP/nativeSetSpatialWidth/nativeSetHarmonicGain)
// que la UI usa como "controlador global" fuera del pipeline AudioFlinger.
// El pipeline AudioFlinger real (omega_process) YA NO lo toca — cada
// instancia usa ctx->fusionCore.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitDSP(JNIEnv* env, jclass clazz, jint sampleRate) {
    if (g_fusionCore != nullptr) {
        delete g_fusionCore;
    }
    g_fusionCore = new IvannaFusionCore(static_cast<float>(sampleRate));
    g_fusionCore->initSpatial(static_cast<float>(sampleRate), 4096);
    LOGI("IvannaFusionCore initialized at %d Hz (spatial renderer active)", sampleRate);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialWidthDirect(JNIEnv* env, jclass clazz, jfloat width) {
    if (g_fusionCore) {
        g_fusionCore->setSpatialWidth(width);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetHarmonicGain(JNIEnv* env, jclass clazz, jfloat gain) {
    if (g_fusionCore) {
        g_fusionCore->setHarmonicGain(gain);
    }
}

}


/* ════════════════════════════════════════════════════════════════════════════
 *  IMPLEMENTACIÓN EFECTO AUDIOFLINGER (GlobalEffect / Magisk soundfx)
 *  Punto de entrada obligatorio: símbolo "AELI"
 *  (AUDIO_EFFECT_LIBRARY_INFO_SYM se expande a AELI vía audio_effect_compat.h)
 * ════════════════════════════════════════════════════════════════════════════ */

/* UUID propio del efecto IVANNA Omega */
static const effect_uuid_t OMEGA_EFFECT_UUID = {
    0x4956414e, 0x4e41, 0x4f4d, 0x4547, {0x41, 0x53, 0x55, 0x50, 0x52, 0x45}
};

static const effect_descriptor_t OMEGA_DESCRIPTOR = {
    {0, 0, 0, 0, {0, 0, 0, 0, 0, 0}},   /* type: null (propósito general) */
    {0x4956414e, 0x4e41, 0x4f4d, 0x4547, {0x41, 0x53, 0x55, 0x50, 0x52, 0x45}},
    EFFECT_CONTROL_API_VERSION,
    EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_ANY,
    100,                                  /* cpuLoad */
    1024,                                 /* memoryUsage (KB) */
    "IVANNA Omega Effect",
    "IVANNA OMEGA SUPREME"
};

/* Contexto por instancia. El PRIMER campo debe ser el puntero a la vtable.
 *
 * AUDIT FIX (session isolation): antes el estado DSP vivía en el global
 * g_fusionCore compartido entre TODAS las sesiones de AudioFlinger. Dos
 * pistas simultáneas (p.ej. música + navegación) pisaban el mismo estado
 * interno (spatial renderer, HRTF, smoothers) -> glitches, saltos de
 * ganancia y cross-talk entre sesiones. Se mueve el IvannaFusionCore a
 * este contexto para que cada instancia tenga el suyo aislado.
 *
 * AUDIT FIX (realtime allocation): buffers L/R deinterleaved preasignados
 * en el contexto (una vez, en SET_CONFIG) para evitar
 * `static thread_local std::vector<float>::resize()` dentro del callback
 * realtime de AudioFlinger. La rama de resize() de std::vector puede
 * llamar a malloc bajo el hilo de audio -> jitter, XRun y en casos
 * extremos deadlock si el allocator toca un mutex compartido.
 */
struct omega_effect_context_t {
    const struct effect_interface_s *itfe;
    effect_config_t config;
    bool enabled;
    IvannaFusionCore* fusionCore;   // AUDIT FIX: DSP per-instance (era global)
    float* rtL;                     // AUDIT FIX: buffer L preasignado (realtime)
    float* rtR;                     // AUDIT FIX: buffer R preasignado (realtime)
    int    rtCapacity;              // frames que caben en rtL/rtR
};

// Capacidad máxima esperada por bloque. AudioFlinger normalmente pasa
// bloques entre 128 y 2048 frames; se reserva un margen holgado para no
// necesitar reasignar nunca en la ruta caliente.
static constexpr int OMEGA_RT_MAX_FRAMES = 8192;

/* ── Funciones de instancia (vtable) ─────────────────────────────────────── */
static int32_t omega_process(effect_handle_t self,
                             audio_buffer_t *inBuf, audio_buffer_t *outBuf) {
    omega_effect_context_t *ctx = reinterpret_cast<omega_effect_context_t *>(self);
    if (!ctx || !ctx->enabled || !inBuf || !outBuf) return 0;
    if (!inBuf->raw || !outBuf->raw || inBuf->frameCount == 0) return 0;

    const int frames = (int)inBuf->frameCount;
    const float* in = inBuf->f32;
    float* out = outBuf->f32;

    // AUDIT FIX (session isolation): usar el fusionCore de ESTA instancia,
    // nunca el global g_fusionCore. Motor aún no configurado: passthrough.
    IvannaFusionCore* fc = ctx->fusionCore;
    if (!fc) {
        memmove(outBuf->raw, inBuf->raw, (size_t)frames * 2u * sizeof(float));
        return 0;
    }

    // AUDIT FIX (realtime allocation): buffers L/R preasignados en el ctx
    // (SET_CONFIG). Si por cualquier razón vinieran sin reservar o el
    // bloque excede la capacidad reservada, se cae a passthrough en vez
    // de asignar en el hilo de audio.
    if (!ctx->rtL || !ctx->rtR || frames > ctx->rtCapacity) {
        memmove(outBuf->raw, inBuf->raw, (size_t)frames * 2u * sizeof(float));
        return 0;
    }
    float* L = ctx->rtL;
    float* R = ctx->rtR;
    for (int n = 0; n < frames; ++n) {
        L[n] = in[2 * n];
        R[n] = in[2 * n + 1];
    }

    // Render binaural de objetos (VBAP + HRTF) + DSP de salida
    fc->processStereo(L, R, (size_t)frames);

    // Interleave -> salida
    for (int n = 0; n < frames; ++n) {
        out[2 * n]     = L[n];
        out[2 * n + 1] = R[n];
    }
    return 0;
}

static int32_t omega_command(effect_handle_t self, uint32_t cmdCode,
                             uint32_t cmdSize, void *pCmdData,
                             uint32_t *replySize, void *pReplyData) {
    omega_effect_context_t *ctx = reinterpret_cast<omega_effect_context_t *>(self);
    switch (cmdCode) {
        case EFFECT_CMD_INIT:
        case EFFECT_CMD_RESET:
            break;
        case EFFECT_CMD_SET_CONFIG:
            if (pCmdData && cmdSize == sizeof(effect_config_t)) {
                ctx->config = *reinterpret_cast<effect_config_t *>(pCmdData);
                uint32_t sr = ctx->config.outputCfg.samplingRate;
                if (sr == 0) sr = ctx->config.inputCfg.samplingRate;
                if (sr == 0) sr = 48000;
                // AUDIT FIX (session isolation): DSP se instancia POR CONTEXTO.
                // Cada sesión AudioFlinger llega aquí y crea su propio
                // IvannaFusionCore; ya no se pisa el global entre sesiones.
                if (!ctx->fusionCore) {
                    ctx->fusionCore = new IvannaFusionCore((float)sr);
                }
                ctx->fusionCore->initSpatial((float)sr, 4096);
                // AUDIT FIX (realtime allocation): preasignar buffers L/R
                // AQUÍ (fuera de la ruta caliente) para que omega_process
                // no tenga que llamar a malloc/resize en el callback.
                if (!ctx->rtL) {
                    ctx->rtL = reinterpret_cast<float*>(
                        calloc((size_t)OMEGA_RT_MAX_FRAMES, sizeof(float)));
                }
                if (!ctx->rtR) {
                    ctx->rtR = reinterpret_cast<float*>(
                        calloc((size_t)OMEGA_RT_MAX_FRAMES, sizeof(float)));
                }
                ctx->rtCapacity =
                    (ctx->rtL && ctx->rtR) ? OMEGA_RT_MAX_FRAMES : 0;
                // Dataset HRTF personalizado (si existe). Fallback: sintético.
                // El resultado se logea SIEMPRE: es el unico punto del sistema
                // donde se decide entre HRTF medido y HRTF sintetico, y esa
                // decision cambia por completo la calidad de la espacializacion.
                static const char* kHrtfPath =
                    "/data/adb/ivanna_omega/hrtf_dataset.ihr1";
                errno = 0;
                if (ctx->fusionCore->loadCustomHrtf(kHrtfPath)) {
                    LOGI("Custom HRTF dataset loaded from %s (measured path ACTIVE)",
                         kHrtfPath);
                } else {
                    // errno solo es significativo si el fallo vino del open();
                    // si el archivo existe pero la cabecera IHR1 es invalida,
                    // errno queda en 0 y hay que decirlo en vez de imprimir
                    // "Success", que seria peor que no logear nada.
                    const int err = errno;
                    LOGW("Failed to load custom HRTF dataset from %s (errno=%d, %s). "
                         "Falling back to SYNTHETIC HRTF.",
                         kHrtfPath, err,
                         err != 0 ? strerror(err)
                                  : "file readable but not a valid IHR1 dataset");
                }
            }
            break;
        case EFFECT_CMD_ENABLE:
            ctx->enabled = true;
            break;
        case EFFECT_CMD_DISABLE:
            ctx->enabled = false;
            break;
        case EFFECT_CMD_SET_PARAM:
        case EFFECT_CMD_SET_PARAM_COMMIT:
        case EFFECT_CMD_GET_PARAM:
        case EFFECT_CMD_SET_DEVICE:
        case EFFECT_CMD_SET_VOLUME:
        case EFFECT_CMD_SET_AUDIO_MODE:
            break;
        default:
            break;
    }
    if (replySize && pReplyData && *replySize >= sizeof(int32_t))
        *reinterpret_cast<int32_t *>(pReplyData) = 0;
    return 0;
}

static int32_t omega_get_descriptor(effect_handle_t self,
                                    effect_descriptor_t *pDescriptor) {
    if (pDescriptor) *pDescriptor = OMEGA_DESCRIPTOR;
    return 0;
}

static int32_t omega_process_reverse(effect_handle_t self,
                                     audio_buffer_t *inBuf, audio_buffer_t *outBuf) {
    return 0;
}

static const struct effect_interface_s OMEGA_INTERFACE = {
    omega_process,
    omega_command,
    omega_get_descriptor,
    omega_process_reverse
};

/* ── Funciones de librería (dlopen entry points) ─────────────────────────── */
static int32_t omega_query_num_effects(uint32_t *pNumEffects) {
    if (pNumEffects) *pNumEffects = 1;
    return 0;
}

static int32_t omega_query_effect(uint32_t index, effect_descriptor_t *pDescriptor) {
    if (index != 0 || !pDescriptor) return -EINVAL;
    *pDescriptor = OMEGA_DESCRIPTOR;
    return 0;
}

static int32_t omega_create_effect(const effect_uuid_t *uuid, int32_t sessionId,
                                   int32_t ioId, effect_handle_t *pHandle) {
    if (!pHandle) return -EINVAL;
    omega_effect_context_t *ctx =
        reinterpret_cast<omega_effect_context_t *>(calloc(1, sizeof(omega_effect_context_t)));
    if (!ctx) return -ENOMEM;
    ctx->itfe = &OMEGA_INTERFACE;
    ctx->enabled = false;
    ctx->fusionCore = nullptr;   // AUDIT FIX: init explícito (per-instance DSP)
    ctx->rtL = nullptr;          // AUDIT FIX: buffers RT se reservan en SET_CONFIG
    ctx->rtR = nullptr;
    ctx->rtCapacity = 0;
    *pHandle = reinterpret_cast<effect_handle_t>(ctx);
    return 0;
}

static int32_t omega_release_effect(effect_handle_t handle) {
    // AUDIT FIX (lifecycle leak): antes sólo se hacía free(handle), dejando
    // fugado el IvannaFusionCore alojado en ctx->fusionCore. En sesiones
    // largas de AudioFlinger (crear/destruir efectos por cada foco de
    // audio) esto acumulaba megas de estado DSP (buffers HRTF, spatial
    // renderer, smoothers) hasta OOM del audioserver. Se libera
    // explícitamente el DSP antes de liberar el contexto.
    if (handle) {
        omega_effect_context_t *ctx =
            reinterpret_cast<omega_effect_context_t *>(handle);
        if (ctx->fusionCore) {
            delete ctx->fusionCore;
            ctx->fusionCore = nullptr;
        }
        // AUDIT FIX (realtime allocation): liberar buffers RT preasignados.
        if (ctx->rtL) { free(ctx->rtL); ctx->rtL = nullptr; }
        if (ctx->rtR) { free(ctx->rtR); ctx->rtR = nullptr; }
        ctx->rtCapacity = 0;
        free(ctx);
    }
    return 0;
}

static int32_t omega_get_descriptor_lib(const effect_uuid_t *uuid,
                                        effect_descriptor_t *pDescriptor) {
    if (!pDescriptor) return -EINVAL;
    *pDescriptor = OMEGA_DESCRIPTOR;
    return 0;
}

/* ── SÍMBOLO "AELI" — el que audioserver busca con dlsym() ───────────────── */
extern "C" __attribute__((visibility("default"), used))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    AUDIO_EFFECT_LIBRARY_TAG,
    EFFECT_LIBRARY_API_VERSION,
    "IVANNA Omega Effect Library",
    "IVANNA OMEGA SUPREME",
    omega_query_num_effects,
    omega_query_effect,
    omega_create_effect,
    omega_release_effect,
    omega_get_descriptor_lib
};
