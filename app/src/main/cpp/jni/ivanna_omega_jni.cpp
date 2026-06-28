/*
 * IVANNA-OMEGA-SUPREME — JNI Bridge Unificado OPTIMIZADO (QUIRÚRGICO)
 * © 2025-2026 Luis Uriel Pimentel Pérez · GORE TNS
 * Todos los derechos reservados.
 */

#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <algorithm>
#include <cstring>

#include "../include/dsp_types.h"
#include "../include/ParametricEQ.h"
#include "../include/Compressor.h"
#include "../include/HarmonicExciter.h"
#include "../include/StereoWidener.h"
#include "../include/GainStage.h"

#include "../neuromorphic/pi_lstm_milenio.hpp"
#include "../neuromorphic/lif_neuron_pool.hpp"

#include "../spatial/spatial_engine.h"

#define TAG  "IVANNA_OMEGA"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

using namespace ivanna;

static constexpr int kMaxFrames = 4096;

struct DSPEngine {
    std::mutex              mtx;
    DSPParams               params;
    ParametricEQ            eq;
    Compressor              comp;
    HarmonicExciter         exciter;
    StereoWidener           widener;
    GainStage               gain;
    std::atomic<bool>       ready{false};

    void applyParams() {
        eq.setParams(params);
        comp.setParams(params);
        exciter.setParams(params);
        widener.setParams(params);
        gain.setParams(params);
        ready.store(true, std::memory_order_release);
    }
};

static DSPEngine& dspEngine() {
    static auto g = std::make_unique<DSPEngine>();
    return *g;
}

static PILSTMMilenio& piLstm() {
    static PILSTMMilenio g;
    return g;
}
static std::mutex piMtx;
static std::atomic<bool> piReady{false};

static SpatialState g_spatial{};
static std::atomic<int> g_mode{0};

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeInit(JNIEnv*, jobject, jint sampleRate) {
    auto& e = dspEngine();
    std::lock_guard<std::mutex> lk(e.mtx);
    e.params.sampleRate = (uint32_t)sampleRate;
    e.applyParams();
    LOGI("DSP engine init  sr=%d", sampleRate);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeSetParams(
    JNIEnv*, jobject,
    jfloat drive, jfloat wet,  jfloat mix,
    jfloat alpha, jfloat beta, jfloat gamma,
    jfloat freq,  jfloat resonance,
    jfloat low,   jfloat mid,  jfloat high,
    jfloat presence, jfloat master
) {
    auto& e = dspEngine();
    std::lock_guard<std::mutex> lk(e.mtx);
    e.params.drive = drive; e.params.wet = wet; e.params.mix = mix;
    e.params.alpha = alpha; e.params.beta = beta; e.params.gamma = gamma;
    e.params.freq = freq;   e.params.resonance = resonance;
    e.params.low = low;     e.params.mid = mid;  e.params.high = high;
    e.params.presence = presence; e.params.master = master;
    e.applyParams();
}

__attribute__((hot, flatten))
JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess(
    JNIEnv* env, jobject,
    jfloatArray buf, jint numFrames
) {
    auto& e = dspEngine();
    if (__builtin_expect(!e.ready.load(std::memory_order_acquire), 0)) return;

    float* __restrict__ data = env->GetFloatArrayElements(buf, nullptr);
    if (__builtin_expect(!data, 0)) return;

    const int n = (numFrames < kMaxFrames) ? numFrames : kMaxFrames;

    alignas(64) float lBuf[kMaxFrames];
    alignas(64) float rBuf[kMaxFrames];

    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; ++i) {
        lBuf[i] = data[i * 2];
        rBuf[i] = data[i * 2 + 1];
    }

    {
        std::lock_guard<std::mutex> lk(e.mtx);
        e.gain.processInput(lBuf, rBuf, n);
        e.exciter.process(lBuf, rBuf, n);
        e.comp.process(lBuf, rBuf, n);
        e.eq.process(lBuf, rBuf, n);
        e.widener.process(lBuf, rBuf, n);
        e.gain.processOutput(lBuf, rBuf, n);
    }

    int mode = g_mode.load(std::memory_order_acquire);
    if (__builtin_expect(mode >= 1 && piReady.load(std::memory_order_acquire), 0)) {
        std::lock_guard<std::mutex> lk(piMtx);
        int done = 0;
        while (done < n) {
            int chunk = (n - done) < BLOCK ? (n - done) : BLOCK;
            alignas(64) float oL[BLOCK], oR[BLOCK];
            piLstm().process_block(lBuf + done, rBuf + done, oL, oR);
            memcpy(lBuf + done, oL, chunk * sizeof(float));
            memcpy(rBuf + done, oR, chunk * sizeof(float));
            done += chunk;
        }
    }

    if (__builtin_expect(mode >= 2, 0)) {
        alignas(64) float outBuf[kMaxFrames];
        spatial_process(lBuf, outBuf, n, &g_spatial);
        const float w0 = 0.7f, w1 = 0.3f;
        #pragma clang loop vectorize(enable) interleave(enable)
        for (int i = 0; i < n; ++i) {
            lBuf[i] = fmaf(w1, outBuf[i], w0 * lBuf[i]);
            rBuf[i] = fmaf(w1, outBuf[i], w0 * rBuf[i]);
        }
    }

    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < n; ++i) {
        data[i * 2]     = lBuf[i];
        data[i * 2 + 1] = rBuf[i];
    }

    env->ReleaseFloatArrayElements(buf, data, 0);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeReset(JNIEnv*, jobject) {
    auto& e = dspEngine();
    std::lock_guard<std::mutex> lk(e.mtx);
    e.eq.reset(); e.comp.reset(); e.exciter.reset(); e.gain.reset();
    e.ready.store(false, std::memory_order_release);
    LOGI("DSP reset");
}

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeVersion(JNIEnv* env, jobject) {
    return env->NewStringUTF("IVANNA-OMEGA-SUPREME v1.0 | GORE TNS");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeInit(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().init();
    piReady.store(true, std::memory_order_release);
    LOGI("PI-LSTM Milenio initialized");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetAlpha(JNIEnv*, jobject, jfloat v) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().set_alpha(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetBeta(JNIEnv*, jobject, jfloat v) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().set_beta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetGamma(JNIEnv*, jobject, jfloat v) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().set_gamma(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetDelta(JNIEnv*, jobject, jfloat v) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().set_delta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHarmonicGain(JNIEnv*, jobject, jfloat v) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().set_harmonic_gain(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHrtfEnabled(JNIEnv*, jobject, jboolean en) {
    std::lock_guard<std::mutex> lk(piMtx);
    piLstm().set_hrtf_enabled(en);
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetNpSat(JNIEnv*, jobject) {
    return piLstm().get_np_sat();
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetError(JNIEnv*, jobject) {
    return piLstm().get_error();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeSetMode(JNIEnv*, jobject, jint mode) {
    g_mode.store(mode, std::memory_order_release);
    LOGI("Processing mode -> %d", mode);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeGetMode(JNIEnv*, jobject) {
    return g_mode.load(std::memory_order_acquire);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitSpatialEngine(JNIEnv*, jobject, jint, jint) {
    memset(&g_spatial, 0, sizeof(g_spatial));
    g_spatial.mu = 500;
    return JNI_TRUE;
}

__attribute__((hot, flatten))
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRenderSpatialBlock(
    JNIEnv* env, jobject,
    jfloatArray input, jfloatArray outL, jfloatArray outR,
    jint posX, jint posY, jint posZ, jint mu
) {
    g_spatial.posX = posX; g_spatial.posY = posY; g_spatial.posZ = posZ;
    g_spatial.mu   = mu;

    float* __restrict__ in = env->GetFloatArrayElements(input, nullptr);
    float* __restrict__ oL = env->GetFloatArrayElements(outL, nullptr);
    float* __restrict__ oR = env->GetFloatArrayElements(outR, nullptr);
    jsize n = env->GetArrayLength(input);

    if (__builtin_expect(!in || !oL || !oR || n <= 0, 0)) {
        if (in) env->ReleaseFloatArrayElements(input, in, JNI_ABORT);
        if (oL) env->ReleaseFloatArrayElements(outL, oL, 0);
        if (oR) env->ReleaseFloatArrayElements(outR, oR, 0);
        return 0;
    }

    spatial_process(in, oL, n, &g_spatial);
    memcpy(oR, oL, n * sizeof(float));

    env->ReleaseFloatArrayElements(input, in, JNI_ABORT);
    env->ReleaseFloatArrayElements(outL, oL, 0);
    env->ReleaseFloatArrayElements(outR, oR, 0);
    return (jint)n;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeReleaseSpatialEngine(JNIEnv*, jobject) {
    memset(&g_spatial, 0, sizeof(g_spatial));
    return JNI_TRUE;
}

} // extern "C"
