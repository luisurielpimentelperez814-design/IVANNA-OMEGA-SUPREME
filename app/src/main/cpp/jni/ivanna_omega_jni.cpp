/*
 * ivanna_omega_jni.cpp
 * JNI bridge for IVANNA OMEGA SUPREME
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */

#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <cmath>
#include "../include/dsp_types.h"
#include "../include/ParametricEQ.h"
#include "../include/Compressor.h"
#include "../include/HarmonicExciter.h"
#include "../include/StereoWidener.h"
#include "../include/GainStage.h"
#include "../neuromorphic/pi_lstm_milenio.hpp"

#define LOG_TAG "IVANNA-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace ivanna;

// ── Engine singletons ──────────────────────────────────────────────────────────
static ParametricEQ         g_eq;
static Compressor           g_comp;
// Use ivanna::HarmonicExciter from include (not pi_lstm_milenio.hpp)
static ivanna::HarmonicExciter g_exciter;
static StereoWidener        g_widener;
static GainStage            g_gain;

// PI-LSTM Milenio engine
static PILSTMMilenioEngine  g_piLstm;

static DSPParams            g_params;
static std::atomic<bool>    g_initialized{false};

// ── Helper: copy jfloatArray ─────────────────────────────────────────────────
static inline bool copyJFloatArray(JNIEnv* env, jfloatArray src, float* dst, int n) {
    if (!src || !dst || n <= 0) return false;
    jfloat* tmp = env->GetFloatArrayElements(src, nullptr);
    if (!tmp) return false;
    memcpy(dst, tmp, n * sizeof(float));
    env->ReleaseFloatArrayElements(src, tmp, JNI_ABORT);
    return true;
}

// ── JNI: Initialization ───────────────────────────────────────────────────────
extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeInitDSP(JNIEnv*, jobject, jint sr) {
    if (sr < 8000 || sr > 192000) {
        LOGE("Invalid sample rate: %d", sr);
        return JNI_FALSE;
    }
    g_params.sampleRate = static_cast<uint32_t>(sr);
    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
    g_piLstm.reset();
    g_initialized.store(true, std::memory_order_release);
    LOGI("DSP initialized @ %d Hz", sr);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeProcessBlock(
    JNIEnv* env, jobject,
    jfloatArray inL, jfloatArray inR,
    jfloatArray outL, jfloatArray outR,
    jint frames) {

    if (!g_initialized.load(std::memory_order_acquire)) return;
    if (frames <= 0) return;

    float lBuf[2048], rBuf[2048], oL[2048], oR[2048];
    int n = (frames > 2048) ? 2048 : frames;

    if (!copyJFloatArray(env, inL, lBuf, n)) return;
    if (!copyJFloatArray(env, inR, rBuf, n)) return;

    // Process through DSP chain
    g_eq.process(lBuf, rBuf, n);
    g_comp.process(lBuf, rBuf, n);
    g_exciter.process(lBuf, rBuf, n);
    g_widener.process(lBuf, rBuf, n);
    g_gain.processInput(lBuf, rBuf, n);
    g_gain.processOutput(lBuf, rBuf, n);

    // PI-LSTM processing
    for (int done = 0; done < n; done += BLOCK) {
        int chunk = (n - done < BLOCK) ? (n - done) : BLOCK;
        g_piLstm.process_block(lBuf + done, rBuf + done, oL, oR, chunk);
        memcpy(lBuf + done, oL, chunk * sizeof(float));
        memcpy(rBuf + done, oR, chunk * sizeof(float));
    }

    // Copy back
    jfloat* outLPtr = env->GetFloatArrayElements(outL, nullptr);
    jfloat* outRPtr = env->GetFloatArrayElements(outR, nullptr);
    if (outLPtr && outRPtr) {
        memcpy(outLPtr, lBuf, n * sizeof(float));
        memcpy(outRPtr, rBuf, n * sizeof(float));
    }
    if (outLPtr) env->ReleaseFloatArrayElements(outL, outLPtr, 0);
    if (outRPtr) env->ReleaseFloatArrayElements(outR, outRPtr, 0);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetParams(
    JNIEnv* env, jobject, jfloatArray params) {
    if (!params) return;
    jfloat* p = env->GetFloatArrayElements(params, nullptr);
    if (!p) return;
    int n = env->GetArrayLength(params);
    if (n >= 1) g_params.drive = p[0];
    if (n >= 2) g_params.wet = p[1];
    if (n >= 3) g_params.mix = p[2];
    if (n >= 4) g_params.alpha = p[3];
    if (n >= 5) g_params.beta = p[4];
    if (n >= 6) g_params.gamma = p[5];
    if (n >= 7) g_params.freq = p[6];
    if (n >= 8) g_params.resonance = p[7];
    if (n >= 9) g_params.low = p[8];
    if (n >= 10) g_params.mid = p[9];
    if (n >= 11) g_params.high = p[10];
    if (n >= 12) g_params.presence = p[11];
    if (n >= 13) g_params.master = p[12];
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);

    g_eq.setParams(g_params);
    g_comp.setParams(g_params);
    g_exciter.setParams(g_params);
    g_widener.setParams(g_params);
    g_gain.setParams(g_params);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeResetDSP(JNIEnv*, jobject) {
    g_eq.reset();
    g_comp.reset();
    g_exciter.reset();
        g_gain.reset();
    g_piLstm.reset();
    LOGI("DSP reset");
}

// PI-LSTM Milenio setters
JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetAlpha(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_alpha(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetBeta(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_beta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetGamma(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_gamma(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetDelta(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_delta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetEta(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_eta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetHarmonicGain(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_harmonic_gain(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetHRTFEnabled(JNIEnv*, jobject, jboolean en) {
    g_piLstm.set_hrtf_enabled(en == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetAdaptEnabled(JNIEnv*, jobject, jboolean en) {
    g_piLstm.set_adapt_enabled(en == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetNPMax(JNIEnv*, jobject, jfloat v) {
    g_piLstm.set_np_max(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetReflectionGain(JNIEnv*, jobject, jint i, jfloat g) {
    g_piLstm.set_reflection_gain(i, g);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeSetReflectionDelay(JNIEnv*, jobject, jint i, jfloat d) {
    g_piLstm.set_reflection_delay(i, d);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeInitPILSTM(JNIEnv*, jobject) {
    g_piLstm.reset();
    LOGI("PI-LSTM initialized");
}

} // extern "C"
