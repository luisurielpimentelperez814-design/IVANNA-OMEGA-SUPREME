// saf_room_jni.cpp — JNI bridge for Φ_SAF-Room^∞ (SaFRoomOptimizer)
//
// Kotlin side: com.ivanna.omega.saf.SaFRoomBridge
//
// Each function maps 1:1 to the SaFRoomOptimizer API:
//   nativeSafrStep()                → step()         → returns α*
//   nativeSafrSetRoom(rt60,drr,mode)→ setRoomState()
//   nativeSafrSetHrtf(mismatch,conv)→ setHrtfState()
//   nativeSafrSetField(diff,comp)   → setSoundFieldState()
//   nativeSafrSetTarget(FloatArray) → setTarget()
//   nativeSafrGetParams():FloatArray→ getParams()
//   nativeSafrDiag():FloatArray     → [alpha,E,lambda,sigma,iter]
//   nativeSafrReset()               → reset()

#include "saf_room_optimizer.hpp"
#include <jni.h>

using ivanna::safRoomOptimizer;

extern "C" {

// α*(R_t, H_t, S_t) — one Riemannian step; returns the step size used
JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrStep(JNIEnv*, jclass) {
    return safRoomOptimizer().step();
}

// Room acoustics context R_t
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetRoom(
        JNIEnv*, jclass, jfloat rt60, jfloat drr, jfloat roomMode) {
    safRoomOptimizer().setRoomState({rt60, drr, roomMode});
}

// HRTF state H_t
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetHrtf(
        JNIEnv*, jclass, jfloat mismatch, jfloat convRate) {
    safRoomOptimizer().setHrtfState({mismatch, convRate});
}

// Sound-field state S_t
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetField(
        JNIEnv*, jclass, jfloat diffuseness, jfloat complexity) {
    safRoomOptimizer().setSoundFieldState({diffuseness, complexity});
}

// Set perceptual target τ_t (float[7])
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetTarget(
        JNIEnv* env, jclass, jfloatArray jtau) {
    if (!jtau || env->GetArrayLength(jtau) < 7) return;
    jfloat* tau = env->GetFloatArrayElements(jtau, nullptr);
    safRoomOptimizer().setTarget(tau);
    env->ReleaseFloatArrayElements(jtau, tau, JNI_ABORT);
}

// Current latent params p_t → float[7]
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrGetParams(JNIEnv* env, jclass) {
    float buf[7]{};
    safRoomOptimizer().getParams(buf);
    jfloatArray arr = env->NewFloatArray(7);
    env->SetFloatArrayRegion(arr, 0, 7, buf);
    return arr;
}

// Diagnostics → float[5]: [alpha*, E_t, λ_t, σ, iteration]
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrDiag(JNIEnv* env, jclass) {
    float buf[5]{
        safRoomOptimizer().getLastAlpha(),
        safRoomOptimizer().getLastError(),
        safRoomOptimizer().getLastLambda(),
        safRoomOptimizer().getLastSigma(),
        static_cast<float>(safRoomOptimizer().getIteration())
    };
    jfloatArray arr = env->NewFloatArray(5);
    env->SetFloatArrayRegion(arr, 0, 5, buf);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrReset(JNIEnv*, jclass) {
    safRoomOptimizer().reset();
}

} // extern "C"
