/*
 * pi_lstm_bridge_jni.cpp
 * JNI bindings for com.ivanna.omega.neuromorphic.PiLstmBridge (Kotlin object).
 *
 * FIX: Resolves UnsatisfiedLinkError on
 *   Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeInit
 * and the rest of the PiLstmBridge external methods. Previously these symbols
 * were not exported by libivanna_omega.so, so the static initializer of the
 * Kotlin `object PiLstmBridge` crashed when DashboardScreen first read
 * `PiLstmBridge.isReady` (MainActivity.kt:169).
 *
 * This file is intentionally small and self-contained. It owns its own
 * PILSTMMilenioEngine instance (g_piLstmBridge), independent from the one
 * used by IvannaNativeLib/DSPBridge, so the two surfaces can coexist without
 * fighting over state. If you want to share state, swap g_piLstmBridge for
 * the existing g_piLstm symbol (extern it from ivanna_omega_jni.cpp).
 *
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */

#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <cmath>

#include "../neuromorphic/pi_lstm_milenio.hpp"

#define LOG_TAG "IVANNA-JNI-PILSTM"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using ivanna::PILSTMMilenioEngine;

// Dedicated engine for the PiLstmBridge Kotlin surface.
static PILSTMMilenioEngine     g_piLstmBridge;
static std::atomic<bool>       g_piLstmBridge_ready{false};

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeInit(JNIEnv*, jobject) {
    g_piLstmBridge.reset();
    g_piLstmBridge_ready.store(true, std::memory_order_release);
    LOGI("PiLstmBridge.nativeInit: PI-LSTM Milenio engine ready");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetAlpha(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_alpha(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetBeta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_beta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetGamma(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_gamma(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetDelta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_delta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHarmonicGain(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_harmonic_gain(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHrtfEnabled(JNIEnv*, jobject, jboolean en) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_hrtf_enabled(en == JNI_TRUE);
}

/*
 * NOTE: PILSTMMilenioEngine does not expose a dedicated NP saturation metric
 * nor an explicit "error" signal. We surface |lstm.h| (saturation proxy,
 * already clamped to [-NP_max, NP_max] inside rk4_step) and 0.0f as a
 * placeholder error so the Kotlin side has stable, finite values. Replace
 * with real telemetry once the engine exposes it.
 */
JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetNpSat(JNIEnv*, jobject) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return 0.0f;
    float h = g_piLstmBridge.lstm.h;
    if (!std::isfinite(h)) return 0.0f;
    return std::fabs(h);
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetError(JNIEnv*, jobject) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return 0.0f;
    // TODO: replace with real residual/error metric from the engine.
    return 0.0f;
}

} // extern "C"
