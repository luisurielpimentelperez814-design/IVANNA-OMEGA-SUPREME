// ─────────────────────────────────────────────────────────────────────────────
// SaFJniBridge.cpp — JNI bridge: com.ivanna.omega.saf.SaFBridge ↔ SaFOptimizer
// ─────────────────────────────────────────────────────────────────────────────
#include <jni.h>
#include "SaFOptimizer.hpp"
#include "SafGlobalBridge.hpp"
#include <android/log.h>

#define SAF_JNI_TAG "SaFJni"

// ── Singleton optimizer instance ─────────────────────────────────────────────
static Ivanna::SaFOptimizer& g_saf = Ivanna::getGlobalSaF();

// Cable SAF → FusionCore (definida en omega_effect.cpp).
// Propaga q_t al ObjectRenderer activo tras cada paso de calibración.
// Si el engine no está inicializado es no-op.
extern "C" void ivanna_saf_apply_latent(const float q[7]);

extern "C" {

// bool nativeSaFInit(String jsonPath)
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFInit(JNIEnv* env, jobject, jstring jPath) {
    const char* path = env->GetStringUTFChars(jPath, nullptr);
    const bool  ok   = g_saf.initFromJson(path);
    env->ReleaseStringUTFChars(jPath, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

// void nativeSaFFeedback(int direction, boolean correct)
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFFeedback(
        JNIEnv*, jobject, jint dir, jboolean correct) {
    g_saf.feedFeedback(static_cast<int>(dir), correct == JNI_TRUE);

    // Cable SAF → convolver: leer q_t actualizado y propagarlo al ObjectRenderer.
    // Sin esto, el optimizador convergía pero el audio nunca cambiaba.
    float q[Ivanna::SAF_K];
    g_saf.getParams(q);
    ivanna_saf_apply_latent(q);
}

// FloatArray? nativeSaFGetParams()  — 7 floats [q0..q6]
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFGetParams(JNIEnv* env, jobject) {
    float buf[Ivanna::SAF_K];
    g_saf.getParams(buf);
    jfloatArray arr = env->NewFloatArray(Ivanna::SAF_K);
    if (arr) env->SetFloatArrayRegion(arr, 0, Ivanna::SAF_K, buf);
    return arr;
}

// int nativeSaFGetIteration()
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFGetIteration(JNIEnv*, jobject) {
    return static_cast<jint>(g_saf.getIteration());
}

// void nativeSaFReset()
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFReset(JNIEnv*, jobject) {
    g_saf.reset();
}

// boolean nativeSaFIsConverged()
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFIsConverged(JNIEnv*, jobject) {
    return g_saf.isConverged() ? JNI_TRUE : JNI_FALSE;
}

// float nativeSaFGetError()
JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFGetError(JNIEnv*, jobject) {
    return g_saf.getErrorEnergy();
}

// boolean nativeSaFSaveState(String path)
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFSaveState(JNIEnv* env, jobject, jstring jPath) {
    const char* path = env->GetStringUTFChars(jPath, nullptr);
    const bool  ok   = g_saf.saveState(path);
    env->ReleaseStringUTFChars(jPath, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

// boolean nativeSaFLoadState(String path)
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeSaFLoadState(JNIEnv* env, jobject, jstring jPath) {
    const char* path = env->GetStringUTFChars(jPath, nullptr);
    const bool  ok   = g_saf.loadState(path);
    env->ReleaseStringUTFChars(jPath, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
