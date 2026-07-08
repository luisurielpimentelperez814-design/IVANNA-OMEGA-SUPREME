/*
 * omega_vibratory_stub.cpp
 * Stub JNI para IvannaNpeNative (com.ivanna.omega.neuromorphic.IvannaNpeNative)
 *
 * NOTA (regla de oro: no se borra, sólo se documenta): este archivo NUNCA
 * estuvo en ningún target de CMakeLists.txt, y "omega_vibratory" nunca
 * existió como librería real. IvannaNpeNative.kt cargaba ese nombre
 * inexistente y fallaba siempre — la telemetría/controles NPE quedaban
 * en no-op. FIX: IvannaNpeNative.kt ahora carga "ivanna_omega", donde
 * jni/ivanna_npe_jni.cpp (implementación real, completa, 23 funciones)
 * ya estaba compilado. Este stub queda huérfano/sin usar intencionalmente.
 *
 * Carga (nunca activa): System.loadLibrary("omega_vibratory")
 *
 * Propósito original: evitar UnsatisfiedLinkError al cargar la librería.
 * La implementación completa del motor NPE está en jni/ivanna_npe_jni.cpp
 * (NO en neuromorphic/ivanna_npe_engine.cpp — ese archivo es en realidad
 * una copia de hexagon/ivanna_fastrpc_client.cpp, nombre engañoso).
 *
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 */

#include <jni.h>
#include <android/log.h>

#define LOG_TAG "IVANNA-VIBRATORY-STUB"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

static constexpr const char* BUILD_TAG = "IVANNA-NPE-STUB-v1.0";
static constexpr const char* COPYRIGHT  = "© 2026 Luis Uriel Pimentel Pérez — GORE TNS";

extern "C" {

// ── Lifecycle ─────────────────────────────────────────────────────────────────
JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeCreate(
    JNIEnv*, jclass, jfloat sr, jint maxBlk) {
    LOGW("nativeCreate stub (sr=%.0f maxBlk=%d)", sr, maxBlk);
    return 0L;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDestroy(
    JNIEnv*, jclass, jlong) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeReset(
    JNIEnv*, jclass, jlong) {}

// ── Processing ────────────────────────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeProcess(
    JNIEnv*, jclass, jlong, jobject, jobject, jint) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeProcessStereo(
    JNIEnv*, jclass, jlong, jobject, jobject, jobject, jobject, jint) {}

// ── Parameters ───────────────────────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetParameters(
    JNIEnv*, jclass, jlong,
    jfloat, jfloat, jfloat, jfloat,
    jfloat, jfloat,
    jfloat, jfloat, jfloat,
    jfloat, jfloat, jfloat, jfloat) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetAGC(
    JNIEnv*, jclass, jlong, jfloat, jfloat) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetBypass(
    JNIEnv*, jclass, jlong, jboolean) {}

// ── Metrics ───────────────────────────────────────────────────────────────────
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetMetrics(
    JNIEnv*, jclass, jlong) { return nullptr; }

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSnapshotScope(
    JNIEnv*, jclass, jlong, jobject, jint) { return 0; }

// ── Engine flags / neuro params ───────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetEngineFlags(
    JNIEnv*, jclass, jlong, jboolean, jboolean, jboolean) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetNeuroParams(
    JNIEnv*, jclass, jlong, jfloat, jfloat, jfloat, jfloat) {}

// ── Synth / genre analysis ────────────────────────────────────────────────────
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetDetectedGenre(
    JNIEnv* env, jclass) {
    return env->NewStringUTF("UNKNOWN");
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetSynthSignature(
    JNIEnv* env, jclass) {
    jfloatArray arr = env->NewFloatArray(5);
    return arr;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetSynthClassify(
    JNIEnv* env, jclass) {
    jfloatArray arr = env->NewFloatArray(7);
    return arr;
}

// ── Build info ────────────────────────────────────────────────────────────────
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetCopyright(
    JNIEnv* env, jclass) {
    return env->NewStringUTF(COPYRIGHT);
}

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetBuildTag(
    JNIEnv* env, jclass) {
    return env->NewStringUTF(BUILD_TAG);
}

// ── Hexagon DSP API ───────────────────────────────────────────────────────────
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspOpen(
    JNIEnv*, jclass, jint, jint, jint) { return JNI_FALSE; }

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspClose(
    JNIEnv*, jclass) {}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspIsAvailable(
    JNIEnv*, jclass) { return JNI_FALSE; }

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspSetActive(
    JNIEnv*, jclass, jboolean) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspSetNeuroParams(
    JNIEnv*, jclass, jfloat, jfloat, jfloat, jfloat, jfloat,
    jfloat, jfloat, jfloat) {}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspGetMetrics(
    JNIEnv*, jclass) { return nullptr; }

} // extern "C"
