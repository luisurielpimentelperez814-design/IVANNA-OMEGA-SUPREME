#include <jni.h>
/*
 * omega_effect.cpp — Audio Effect Plugin OPTIMIZADO v1.1 (FIXED)
 * FIXES:
 * 1. Null check for out buffer in Effect_Process
 * 2. NaN propagation guard in AGC
 * 3. Memory order strengthened in LockFreeRing
 */
#include "audio_effect_compat.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#include "omega_shared.h"

#ifndef AUDIO_CHANNEL_OUT_STEREO
#define AUDIO_CHANNEL_OUT_STEREO 0x3u
#endif

#ifndef AUDIO_FORMAT_PCM_FLOAT
#define AUDIO_FORMAT_PCM_FLOAT 0x5u
#endif

#define LOG_TAG "OmegaEffect"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static constexpr const char* kSocketName = "omega_daemon_socket";
static constexpr float kAgcTargetRms = 0.126f;
static constexpr float kAgcGainMin = 0.25f;
static constexpr float kAgcGainMax = 4.0f;

// AUDIT NOTE (crítico, riesgo de hardware): kEffectTypeNull DEBE seguir
// siendo el EFFECT_UUID_NULL canónico de AOSP (ec7178a0-847d-11e0-...).
// Es el sentinel estándar para "efecto de tipo no predefinido" — correcto
// para un insert effect propietario como OMEGA.
//
// NO reemplazar por el UUID de "dap" (9d4921da-8225-4f29-aefa-39537a04bcaa)
// que aparece en vendor/etc/audio_effects.xml y
// magisk_module/vendor_base/sku_blair_audio_effects.xml — ese es el UUID
// real de un efecto DAP (Dynamic Audio Processing) del stack del vendor,
// posiblemente ligado a protección de altavoz / límites térmicos del SoC.
// Copiarlo aquí haría que OMEGA se anuncie ante AudioPolicyManager con el
// mismo `type` que un efecto real del sistema — riesgo de colisión en la
// resolución de la cadena de efectos (enmascarar, ser enmascarado, o ser
// rechazado por duplicado). Si en algún momento se quiere interceptar el
// slot de "dap" a propósito, eso requiere entender primero exactamente qué
// hace ese efecto en este firmware — no es un cambio de una línea.
static const effect_uuid_t kEffectTypeNull = {
    0xec7178a0,0x847d,0x11e0,0xa3cb,{0x00,0x02,0xa5,0xd5,0xc5,0x1b}};
static const effect_uuid_t kEffectUuid = {
    0x8d7d5e0a,0xa6eb,0x4fde,0xa0ff,{0xcb,0x1b,0x2d,0xd7,0x27,0x5e}};
static const effect_descriptor_t kDesc = {
    .type=kEffectTypeNull,.uuid=kEffectUuid,
    .apiVersion=EFFECT_CONTROL_API_VERSION,
    .flags=EFFECT_FLAG_TYPE_INSERT|EFFECT_FLAG_INSERT_LAST,
    .cpuLoad=30,.memoryUsage=200,
    .name="OMEGA Omega_in AI Bridge",.implementor="GORE TNS"};

struct OmegaContext {
    const struct effect_interface_s* itfe;
    effect_config_t config;
    bool active = false;
    OmegaSharedState* shared = nullptr;
    float agc_envelope = kAgcTargetRms;
    float agc_gain = 1.0f;
    std::atomic<uint32_t> underruns{0};
    // Anti-Dolby v1.5: detección EAC3
    bool antiDolbyActive = false;
    bool isEac3Decoder = false;
};

static int receive_shm_fd() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct timeval tv{0, 200000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path+1, kSocketName, sizeof(addr.sun_path)-2);
    socklen_t alen = (socklen_t)(sizeof(addr.sun_family)+1+strlen(kSocketName));
    if (connect(sock, (sockaddr*)&addr, alen) < 0) { close(sock); return -1; }
    char buf = 0;
    struct iovec iov{&buf,1};
    char cmsg[CMSG_SPACE(sizeof(int))];
    struct msghdr msg{};
    msg.msg_iov=&iov; msg.msg_iovlen=1;
    msg.msg_control=cmsg; msg.msg_controllen=sizeof(cmsg);
    if (recvmsg(sock, &msg, 0) < 0) { close(sock); return -1; }
    close(sock);
    struct cmsghdr* c = CMSG_FIRSTHDR(&msg);
    if (!c||c->cmsg_level!=SOL_SOCKET||c->cmsg_type!=SCM_RIGHTS) return -1;
    int fd=-1; memcpy(&fd, CMSG_DATA(c), sizeof(int)); return fd;
}

static bool mapSharedMemory(OmegaContext* ctx) {
    if (ctx->shared) return true;
    int fd = receive_shm_fd();
    if (fd < 0) return false;
    void* m = mmap(nullptr, sizeof(OmegaSharedState),
        PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { ALOGE("mmap falló"); return false; }
    ctx->shared = static_cast<OmegaSharedState*>(m);
    ALOG("Shm mapeada via SCM_RIGHTS OK");
    return true;
}

static void unmapSharedMemory(OmegaContext* ctx) {
    if (ctx->shared) { munmap(ctx->shared, sizeof(OmegaSharedState)); ctx->shared=nullptr; }
}

// FIX (Adaptive Feedback Loop — cierre de la ruta real de Spotify/YouTube):
// applyAgc() sólo corre si ai_enabled==true, así que ai_rms_level/ai_gain_db
// nunca reflejan audio real cuando el AGC está apagado (su default). Esta
// función corre SIEMPRE, sobre el bloque de salida ya procesado por el
// daemon (ring_out), independiente del estado del AGC — es la fuente real
// que consume el AdaptiveDecisionEngine del lado de la app (mismo proceso,
// via el puente en omega_daemon.cpp/ivanna_omega_jni.cpp). Coste: un solo
// pase sobre 'samples' floats, sin malloc, sin locks — misma forma que el
// cómputo de RMS que ya existe en applyAgc(), sin envelope/smoothing (eso
// es específico del AGC, la telemetría cruda quiere el valor instantáneo
// del bloque, no una versión suavizada).
static void updateRawTelemetry(OmegaContext* ctx, const float* buf, int samples) {
    if (!ctx || !ctx->shared || !buf || samples <= 0) return;

    float sumSq = 0.0f;
    float peak = 0.0f;
    #pragma unroll 8
    for (int i = 0; i < samples; ++i) {
        float s = buf[i];
        if (!std::isfinite(s)) s = 0.0f;
        sumSq += s * s;
        float a = std::fabsf(s);
        if (a > peak) peak = a;
    }
    float rms = sqrtf(sumSq / (float)samples + 1e-12f);
    if (!std::isfinite(rms)) rms = 0.0f;
    if (!std::isfinite(peak)) peak = 0.0f;

    ctx->shared->ai_raw_rms.store(rms, std::memory_order_relaxed);
    ctx->shared->ai_raw_peak.store(peak, std::memory_order_relaxed);
}

static void applyAgc(OmegaContext* ctx, float* buf, int samples) {
    if (!ctx || !buf || samples <= 0) return;

    if (!std::isfinite(ctx->agc_envelope)) {
        ctx->agc_envelope = kAgcTargetRms;
    }
    if (!std::isfinite(ctx->agc_gain)) {
        ctx->agc_gain = 1.0f;
    }

    float sensitivity = ctx->shared->ai_sensitivity.load(std::memory_order_relaxed);
    float alpha = 0.001f + sensitivity * 0.049f;

    float rms = 0.0f;
    #pragma unroll 8
    for (int i = 0; i < samples; ++i) {
        float s = buf[i];
        if (!std::isfinite(s)) s = 0.0f;
        rms += s * s;
    }
    rms = sqrtf(rms / (float)samples + 1e-12f);

    if (!std::isfinite(rms) || rms < 1e-12f) rms = 1e-12f;

    ctx->agc_envelope += alpha * (rms - ctx->agc_envelope);
    ctx->shared->ai_rms_level.store(ctx->agc_envelope, std::memory_order_relaxed);

    float target_gain = (ctx->agc_envelope > 1e-6f)
        ? kAgcTargetRms / ctx->agc_envelope : 1.0f;
    target_gain = std::fmaxf(kAgcGainMin, std::fminf(kAgcGainMax, target_gain));
    ctx->agc_gain += alpha * (target_gain - ctx->agc_gain);

    if (!std::isfinite(ctx->agc_gain)) ctx->agc_gain = 1.0f;

    const float gain = ctx->agc_gain;
    #pragma unroll 8
    for (int i = 0; i < samples; ++i) {
        buf[i] *= gain;
    }

    float gain_db = 20.0f * log10f(std::fmaxf(ctx->agc_gain, 1e-6f));
    ctx->shared->ai_gain_db.store(gain_db, std::memory_order_relaxed);
}

static int Effect_Process(effect_handle_t, audio_buffer_t*, audio_buffer_t*);
static int Effect_Command(effect_handle_t, uint32_t, uint32_t, void*, uint32_t*, void*);
static int Effect_GetDescriptor(effect_handle_t, effect_descriptor_t*);

static const struct effect_interface_s sIface = {
    Effect_Process, Effect_Command, Effect_GetDescriptor, nullptr};

static int Effect_Process(effect_handle_t self,
                          audio_buffer_t* in, audio_buffer_t* out) {
    auto* ctx = (OmegaContext*)self;
    if (!ctx || !in || !out) return -EINVAL;
    int n = (int)in->frameCount; if (n<=0) return 0;
    int ch = audio_channel_count_from_out_mask(ctx->config.inputCfg.channels);
    int samples = n * (ch>0?ch:2);

    bool ok = ctx->active && (ctx->shared || mapSharedMemory(ctx));
    if (!ok || (ctx->shared && ctx->shared->bypass_enabled.load(std::memory_order_relaxed))) {
        if (in->raw!=out->raw) memcpy(out->raw, in->raw, samples*sizeof(float));
        return 0;
    }

    int cap = std::min(samples, OMEGA_BLOCK_SIZE*OMEGA_MAX_CHANNELS);
    if (in->raw!=out->raw) memcpy(out->raw, in->raw, samples*sizeof(float));

    ctx->shared->ring_in.tryPush(in->f32, cap, &ctx->shared->input_buffer[0][0]);
    bool got = ctx->shared->ring_out.tryPop(out->f32, cap, &ctx->shared->output_buffer[0][0]);
    if (!got) ctx->underruns.fetch_add(1, std::memory_order_relaxed);

    // FIX (Adaptive Feedback Loop): telemetría cruda SIEMPRE, sin depender
    // de ai_enabled (AGC) — es la fuente real para AdaptiveDecisionEngine.
    if (got) updateRawTelemetry(ctx, out->f32, cap);

    if (ctx->shared->ai_enabled.load(std::memory_order_relaxed) && got)
        applyAgc(ctx, out->f32, cap);

    return 0;
}

static int Effect_Command(effect_handle_t self, uint32_t cmd, uint32_t csz,
                          void* pCmd, uint32_t* rsz, void* pReply) {
    auto* ctx = (OmegaContext*)self; if (!ctx) return -EINVAL;
    (void)csz;(void)pCmd;
    switch (cmd) {
    case EFFECT_CMD_INIT:
        if (rsz&&*rsz>=sizeof(int)) *(int*)pReply=0; return 0;
    case EFFECT_CMD_CONFIGURE:
        if (csz>=sizeof(effect_config_t)) memcpy(&ctx->config, pCmd, sizeof(effect_config_t));
        ALOG("CONFIGURE fs=%u", ctx->config.inputCfg.samplingRate);
        if (rsz&&*rsz>=sizeof(int)) *(int*)pReply=0; return 0;
    case EFFECT_CMD_RESET:
        ctx->agc_envelope=kAgcTargetRms; ctx->agc_gain=1.0f;
        if (rsz&&*rsz>=sizeof(int)) *(int*)pReply=0; return 0;
    case EFFECT_CMD_ENABLE:
        ctx->active=true; ALOG("ENABLE"); if (rsz&&*rsz>=sizeof(int)) *(int*)pReply=0; return 0;
    case EFFECT_CMD_DISABLE:
        ctx->active=false; ALOG("DISABLE"); if (rsz&&*rsz>=sizeof(int)) *(int*)pReply=0; return 0;
    case EFFECT_CMD_SET_VOLUME:
        if (pReply&&rsz&&*rsz>=sizeof(int32_t)) *(int32_t*)pReply=0; return 0;
    default: return -EINVAL;
    }
}

static int Effect_GetDescriptor(effect_handle_t, effect_descriptor_t* d) {
    if (!d) return -EINVAL; memcpy(d, &kDesc, sizeof(kDesc)); return 0;
}

int EffectCreate(const effect_uuid_t* uuid, int32_t sid, int32_t iid,
                        effect_handle_t* handle) {
    (void)sid;(void)iid;
    if (!uuid||!handle) return -EINVAL;
    if (memcmp(uuid,&kEffectUuid,sizeof(effect_uuid_t))!=0) return -ENOENT;
    auto* ctx = new(std::nothrow) OmegaContext();
    if (!ctx) return -ENOMEM;
    ctx->itfe=&sIface;
    ctx->config.inputCfg.samplingRate=OMEGA_SAMPLE_RATE;
    ctx->config.inputCfg.channels=AUDIO_CHANNEL_OUT_STEREO;
    ctx->config.inputCfg.format=AUDIO_FORMAT_PCM_FLOAT;
    ctx->config.outputCfg=ctx->config.inputCfg;
    mapSharedMemory(ctx);
    *handle=(effect_handle_t)ctx;
    ALOG("EffectCreate OK");
    return 0;
}

int EffectRelease(effect_handle_t h) {
    if (!h) return -EINVAL;
    auto* ctx=(OmegaContext*)h; unmapSharedMemory(ctx); delete ctx;
    ALOG("EffectRelease OK"); return 0;
}

int EffectGetDescriptor(const effect_uuid_t* uuid, effect_descriptor_t* d) {
    if (!uuid||!d) return -EINVAL;
    if (memcmp(uuid,&kEffectUuid,sizeof(effect_uuid_t))!=0&&
        memcmp(uuid,EFFECT_UUID_NULL,sizeof(effect_uuid_t))!=0) return -ENOENT;
    memcpy(d,&kDesc,sizeof(kDesc)); return 0;
}

int QueryNumEffects(uint32_t* n) { if(!n) return -EINVAL; *n=1; return 0; }
int QueryEffect(uint32_t i, effect_descriptor_t* d) {
    if(!d||i!=0) return -ENOENT; memcpy(d,&kDesc,sizeof(kDesc)); return 0; }

extern "C" __attribute__((visibility("default")))
audio_effect_library_t AELI = {
    .tag=AUDIO_EFFECT_LIBRARY_TAG,.version=EFFECT_LIBRARY_API_VERSION,
    .name="OMEGA Omega_in Bridge",.implementor="GORE TNS",
    .query_num_effects=QueryNumEffects,.query_effect=QueryEffect,
    .create_effect=EffectCreate,.release_effect=EffectRelease,
    .get_descriptor=EffectGetDescriptor};

extern "C" {
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_OmegaEffect_nativeInit(JNIEnv*,jobject){ return JNI_TRUE; }
JNIEXPORT void JNICALL Java_com_ivanna_omega_OmegaEffect_nativeRelease(JNIEnv*,jobject){}
JNIEXPORT void JNICALL Java_com_ivanna_omega_OmegaEffect_nativeSetActive(JNIEnv*,jobject,jboolean){}
JNIEXPORT void JNICALL Java_com_ivanna_omega_OmegaEffect_nativeSetIntensity(JNIEnv*,jobject,jfloat){}
JNIEXPORT void JNICALL Java_com_ivanna_omega_OmegaEffect_nativeSetVocoderMix(JNIEnv*,jobject,jfloat){}
}
