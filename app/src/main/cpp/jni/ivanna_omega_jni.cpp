/*
 * IVANNA-OMEGA-SUPREME — JNI Bridge Unificado
 * © 2025-2026 Luis Uriel Pimentel Pérez · GORE TNS
 * Todos los derechos reservados.
 *
 * Fusiona:
 *   • Motor DSP de IVANNA-FUSION-PRO (ParametricEQ/Compressor/HarmonicExciter/
 *     StereoWidener/GainStage — con todos los FIX acumulados)
 *   • Evolutionary kernel de IVANNA-FUSION
 *   • Motor espacial Ω-ATLAS de IVANNA-FUSION
 *   • PI-LSTM Milenio de IVANNA-ULTRA
 *   • IvannaDspManager (Hexagon FastRPC) de IVANNA-ULTRA
 *
 * Paquete Android: com.ivanna.omega
 * Lib nativa: libivanna_omega.so
 */
#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// ── DSP core (FUSION-PRO) ────────────────────────────────────────────────────
#include "../include/dsp_types.h"
#include "../include/ParametricEQ.h"
#include "../include/Compressor.h"
#include "../include/HarmonicExciter.h"
#include "../include/StereoWidener.h"
#include "../include/GainStage.h"

// ── Neuromorphic (ULTRA) ─────────────────────────────────────────────────────
#include "../neuromorphic/pi_lstm_milenio.hpp"
#include "../neuromorphic/lif_neuron_pool.hpp"

// ── Spatial (FUSION) ─────────────────────────────────────────────────────────
#include "../spatial/spatial_engine.h"

#define TAG  "IVANNA_OMEGA"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

using namespace ivanna;

static constexpr int kMaxFrames = 4096;

// ─── DSP Engine singleton (FUSION-PRO grade, race-free) ──────────────────────
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

// ─── PI-LSTM Milenio singleton (ULTRA) ───────────────────────────────────────
static PILSTMMilenio& piLstm() {
    static PILSTMMilenio g;
    return g;
}
static std::mutex piMtx;
static std::atomic<bool> piReady{false};

// ─── Spatial state (FUSION) ──────────────────────────────────────────────────
static SpatialState g_spatial{};

// ─── Process mode ────────────────────────────────────────────────────────────
// 0 = DSP only, 1 = DSP + PI-LSTM, 2 = DSP + PI-LSTM + Spatial
static std::atomic<int> g_mode{0};

// ═════════════════════════════════════════════════════════════════════════════
// DSP JNI  (com.ivanna.omega.dsp.DSPBridge)
// ═════════════════════════════════════════════════════════════════════════════
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

JNIEXPORT void JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess(
    JNIEnv* env, jobject,
    jfloatArray buf, jint numFrames
) {
    auto& e = dspEngine();
    if (!e.ready.load(std::memory_order_acquire)) return;

    jfloat* data = env->GetFloatArrayElements(buf, nullptr);
    if (!data) return;

    const int n = (numFrames > kMaxFrames) ? kMaxFrames : numFrames;
    std::vector<float> lBuf(n), rBuf(n);

    for (int i = 0; i < n; ++i) { lBuf[i] = data[i*2]; rBuf[i] = data[i*2+1]; }

    {
        std::lock_guard<std::mutex> lk(e.mtx);
        e.gain.processInput(lBuf.data(), rBuf.data(), n);
        e.exciter.process(lBuf.data(), rBuf.data(), n);
        e.comp.process(lBuf.data(), rBuf.data(), n);
        e.eq.process(lBuf.data(), rBuf.data(), n);
        e.widener.process(lBuf.data(), rBuf.data(), n);
        e.gain.processOutput(lBuf.data(), rBuf.data(), n);
    }

    // Optional PI-LSTM post-process
    int mode = g_mode.load(std::memory_order_acquire);
    if (mode >= 1 && piReady.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lk(piMtx);
        // Process in BLOCK-sized chunks
        int done = 0;
        while (done < n) {
            int chunk = std::min(n - done, (int)BLOCK);
            float oL[BLOCK], oR[BLOCK];
            piLstm().process_block(lBuf.data() + done, rBuf.data() + done, oL, oR);
            for (int i = 0; i < chunk; ++i) {
                lBuf[done + i] = oL[i];
                rBuf[done + i] = oR[i];
            }
            done += chunk;
        }
    }

    // Optional spatial
    if (mode >= 2) {
        std::vector<float> outBuf(n);
        spatial_process(lBuf.data(), outBuf.data(), n, &g_spatial);
        // blend spatial back to stereo
        for (int i = 0; i < n; ++i) {
            lBuf[i] = 0.7f * lBuf[i] + 0.3f * outBuf[i];
            rBuf[i] = 0.7f * rBuf[i] + 0.3f * outBuf[i];
        }
    }

    for (int i = 0; i < n; ++i) { data[i*2] = lBuf[i]; data[i*2+1] = rBuf[i]; }
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

// ═════════════════════════════════════════════════════════════════════════════
// PI-LSTM JNI  (com.ivanna.omega.neuromorphic.PiLstmBridge)
// ═════════════════════════════════════════════════════════════════════════════

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

// ═════════════════════════════════════════════════════════════════════════════
// Mode control  (com.ivanna.omega.core.OmegaEngine)
// ═════════════════════════════════════════════════════════════════════════════

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeSetMode(JNIEnv*, jobject, jint mode) {
    // 0=DSP, 1=DSP+LSTM, 2=DSP+LSTM+Spatial
    g_mode.store(mode, std::memory_order_release);
    LOGI("Processing mode -> %d", mode);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_OmegaEngine_nativeGetMode(JNIEnv*, jobject) {
    return g_mode.load(std::memory_order_acquire);
}

// ═════════════════════════════════════════════════════════════════════════════
// Spatial JNI  (com.ivanna.omega.core.IvannaNativeLib  — spatial subset)
// ═════════════════════════════════════════════════════════════════════════════

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitSpatialEngine(JNIEnv*, jobject, jint /*sr*/, jint /*buf*/) {
    memset(&g_spatial, 0, sizeof(g_spatial));
    g_spatial.mu = 500;
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRenderSpatialBlock(
    JNIEnv* env, jobject,
    jfloatArray input, jfloatArray outL, jfloatArray outR,
    jint posX, jint posY, jint posZ, jint mu
) {
    g_spatial.posX = posX; g_spatial.posY = posY; g_spatial.posZ = posZ;
    g_spatial.mu   = mu;

    jfloat* in  = env->GetFloatArrayElements(input, nullptr);
    jfloat* oL  = env->GetFloatArrayElements(outL, nullptr);
    jfloat* oR  = env->GetFloatArrayElements(outR, nullptr);
    jsize   n   = env->GetArrayLength(input);

    spatial_process(in, oL, n, &g_spatial);
    for (int i = 0; i < n; i++) oR[i] = oL[i]; // simple mono→stereo for now

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
