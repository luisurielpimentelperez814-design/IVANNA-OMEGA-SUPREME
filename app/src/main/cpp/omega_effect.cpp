#include <jni.h>
#include <android/log.h>
#include "IvannaFusionCore.cpp"
#include <vector>
#include "audio_effect_compat.h"
#include "include/omega_control_bus.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <atomic>

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
 *
 * AUDIT FIX (control plane reconnect): campo lastAppliedGen guarda la
 * última generation del OmegaControlBus (SHM) aplicada por esta instancia.
 * Antes, omega_process ignoraba TOTALMENTE el snapshot publicado por el
 * daemon (command_server publica pero el efecto no consumía): la UI
 * movía sliders pero AudioFlinger corría con los defaults inmutables.
 * Ahora cada instancia lee readLatest() al principio del callback y
 * aplica los parámetros a su ctx->fusionCore, restaurando el puente
 * cross-process UI -> daemon -> audioserver.
 */
struct omega_effect_context_t {
    const struct effect_interface_s *itfe;
    effect_config_t config;
    bool enabled;
    IvannaFusionCore* fusionCore;   // AUDIT FIX: DSP per-instance (era global)
    float* rtL;                     // AUDIT FIX: buffer L preasignado (realtime)
    float* rtR;                     // AUDIT FIX: buffer R preasignado (realtime)
    int    rtCapacity;              // frames que caben en rtL/rtR
    uint64_t lastAppliedGen;        // AUDIT FIX: seguimiento de la generation SHM
    bool     ctrlBusOpen;           // AUDIT FIX: OmegaControlBus reader ready
    bool     chunkedWarned;         // AUDIT FIX: log único cuando se procesa en chunks
};

// Capacidad máxima esperada por bloque. AudioFlinger normalmente pasa
// bloques entre 128 y 2048 frames; se reserva un margen holgado para no
// necesitar reasignar nunca en la ruta caliente.
static constexpr int OMEGA_RT_MAX_FRAMES = 8192;

// AUDIT FIX (control plane reconnect): aplica un OmegaDspSnapshot recibido
// vía SHM al IvannaFusionCore local. Se llama fuera del hot-path (una vez
// por generation nueva). No hace malloc, no bloquea, no logea en cada
// llamada. Los parámetros que el core actual no expone (EQ, dialónboost,
// SAF, etc.) se ignoran hasta que se añadan setters en fases posteriores
// — lo que importa aquí es que los sliders visibles de la UI (spatial
// width, harmonic gain, compresor) SI lleguen al audio real.
static inline void omega_apply_snapshot(IvannaFusionCore* fc,
                                        const ivanna::OmegaDspSnapshot& s) noexcept {
    if (!fc) return;
    // El route arbiter marca quién aplica DSP. Si no somos SYSTEM_WIDE,
    // ni intensity ni el resto tocan; el efecto queda enabled pero pasa.
    if (static_cast<ivanna::RouteMode>(s.active_route) != ivanna::RouteMode::SYSTEM_WIDE) {
        return;
    }
    // Spatial width (slider UI "Ancho espacial")
    if (std::isfinite(s.spatial_width) && s.spatial_width > 0.f) {
        fc->setSpatialWidth(s.spatial_width);
    }
    // Harmonic gain (slider UI "Ganancia armónica")
    if (std::isfinite(s.harmonic_gain) && s.harmonic_gain >= 0.f) {
        fc->setHarmonicGain(s.harmonic_gain);
    }
    // Compresor (threshold / ratio)
    if (std::isfinite(s.compressor) && s.compressor < 0.f) {
        // s.compressor viene en dB negativos (threshold), ratio derivada del
        // campo comp_amount [0..1] mapeado a [1..8]:1 (compat con el driver).
        const float ratio = 1.0f + 7.0f * std::clamp(s.comp_amount, 0.f, 1.f);
        fc->setCompressorParams(s.compressor, ratio);
    }
}

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

    // AUDIT FIX (control plane reconnect): drena a lo más UN snapshot nuevo
    // por callback. readLatest() es lock-free (seqlock en SHM), no bloquea,
    // no malloc, y solo retorna true si hay una generation posterior a la
    // ya aplicada. Esto conecta finalmente los sliders de la UI -> daemon
    // -> SHM -> omega_process, cerrando el ciclo que estaba roto.
    if (ctx->ctrlBusOpen) {
        ivanna::OmegaDspSnapshot snap;
        if (ivanna::effectControlBus().readLatest(snap, ctx->lastAppliedGen)) {
            omega_apply_snapshot(fc, snap);
        }
    }

    // AUDIT FIX (realtime allocation): buffers L/R preasignados en el ctx
    // (SET_CONFIG). Si vinieran sin reservar (calloc falló en SET_CONFIG)
    // se cae a passthrough — jamás asignar en el hilo de audio.
    if (!ctx->rtL || !ctx->rtR || ctx->rtCapacity <= 0) {
        memmove(outBuf->raw, inBuf->raw, (size_t)frames * 2u * sizeof(float));
        return 0;
    }
    float* L = ctx->rtL;
    float* R = ctx->rtR;

    // AUDIT FIX (rigid buffer / silent bypass): si el bloque entrante excede
    // la capacidad preasignada (p.ej. AudioFlinger con LDAC/LHDC puede lanzar
    // bloques grandes), ya NO se hace bypass silencioso a plano. Se procesa
    // el bloque completo en chunks de a lo sumo rtCapacity frames, reutilizando
    // los mismos buffers L/R — sin malloc, sin locks, mismo coste de memoria.
    // Solo se logea UNA vez por instancia (flag en el ctx, no en el hot path)
    // para no inundar logcat desde el callback de audio.
    if (frames > ctx->rtCapacity && !ctx->chunkedWarned) {
        ctx->chunkedWarned = true;
        LOGW("omega_process: frameCount=%d > rtCapacity=%d — procesando en chunks (sin bypass)",
             frames, ctx->rtCapacity);
    }

    int offset = 0;
    while (offset < frames) {
        const int chunk = ((frames - offset) < ctx->rtCapacity)
                          ? (frames - offset) : ctx->rtCapacity;
        const float* inChunk  = in  + (size_t)offset * 2u;
        float*       outChunk = out + (size_t)offset * 2u;

        for (int n = 0; n < chunk; ++n) {
            L[n] = inChunk[2 * n];
            R[n] = inChunk[2 * n + 1];
        }

        // Render binaural de objetos (VBAP + HRTF) + DSP de salida
        fc->processStereo(L, R, (size_t)chunk);

        // Interleave -> salida
        for (int n = 0; n < chunk; ++n) {
            outChunk[2 * n]     = L[n];
            outChunk[2 * n + 1] = R[n];
        }
        offset += chunk;
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
                // AUDIT FIX (control plane reconnect): abrir el reader del
                // OmegaControlBus (SHM cross-process). Si el daemon todavía
                // no creó el SHM (arranque en frío del audioserver), la
                // apertura simplemente falla y el efecto sigue como
                // passthrough respecto al control plane; en el próximo
                // SET_CONFIG (cada apertura de sesión) se reintenta.
                if (!ctx->ctrlBusOpen) {
                    ctx->ctrlBusOpen = ivanna::effectControlBus().openReader();
                    if (ctx->ctrlBusOpen) {
                        // Sembrar el estado inicial con el snapshot más reciente
                        // ya publicado por el daemon — sin esto, el primer
                        // bloque de audio corre con los defaults del core hasta
                        // que llegue el siguiente publish() de la UI.
                        ivanna::OmegaDspSnapshot seed;
                        uint64_t seen = 0;
                        if (ivanna::effectControlBus().readLatest(seed, seen)) {
                            omega_apply_snapshot(ctx->fusionCore, seed);
                            ctx->lastAppliedGen = seen;
                            LOGI("OmegaControlBus attached (seed gen=%llu route=%d)",
                                 (unsigned long long)seen,
                                 (int)seed.active_route);
                        } else {
                            LOGI("OmegaControlBus attached (no snapshot yet)");
                        }
                    }
                }
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
    ctx->lastAppliedGen = 0;     // AUDIT FIX: control plane empieza sin generation
    ctx->ctrlBusOpen    = false;
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

// ── Cable SAF → FusionCore ────────────────────────────────────────────────────
// Llamada desde SaFJniBridge.cpp tras cada feedFeedback() para propagar
// el vector latente q_t del optimizador Φ_SAF^∞ al ObjectRenderer activo.
// No es static para que SaFJniBridge pueda declararla extern.
// Si el engine aún no fue inicializado (g_fusionCore == nullptr), es no-op.
extern "C" void ivanna_saf_apply_latent(const float q[7]) {
    if (g_fusionCore) g_fusionCore->setSafLatentParams(q);
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
