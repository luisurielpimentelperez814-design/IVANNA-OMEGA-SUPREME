// ivanna_route_a_asrc_jni.cpp — Puente JNI del ASRC magistral Ruta A.
//
// Clase Kotlin: com.ivanna.omega.audio.RouteAAsrc (una instancia por
// reproducción). Vive en libivanna_omega.so (target `ivanna_omega`).
//
// Tiempo real: process() usa GetPrimitiveArrayCritical (cero copias, sin
// interacción con el GC) y solo hace cómputo puro dentro de la sección
// crítica (sin llamadas JNI, sin bloqueos, sin allocs). Los setters
// (ppm, Ψ) son atómicos y no tocan el camino de audio.
//
// Fail-closed: si el handle es 0 o el nativo no está, las llamadas no
// hacen nada (el lado Kotlin detecta isAvailable=false y usa el
// remuestreador Kotlin como fallback; el audio nunca se rompe).
#include <jni.h>

#include <cstdint>
#include <new>  // std::nothrow (el NDK no lo trae por transitividad con -fno-exceptions)

#include "ivanna_route_a_asrc.h"

using ivanna::routea::RouteAAsrc;

namespace {

RouteAAsrc* fromHandle(jlong handle) {
    if (handle == 0) return nullptr;
    return reinterpret_cast<RouteAAsrc*>(static_cast<uintptr_t>(handle));
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeCreate(JNIEnv* env, jobject /*thiz*/) {
    (void)env;
    auto* asrc = new (std::nothrow) RouteAAsrc();
    if (!asrc) return 0;
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(asrc));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeDestroy(JNIEnv* env, jobject /*thiz*/,
                                                     jlong handle) {
    (void)env;
    delete fromHandle(handle);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeProcess(JNIEnv* env, jobject /*thiz*/,
                                                     jlong handle, jfloatArray in,
                                                     jint inFrames, jfloatArray out,
                                                     jint outCap) {
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc || !in || !out || inFrames <= 0 || outCap <= 0) return 0;
    // Sección crítica: cero copias. Dentro solo hay cómputo puro
    // (RouteAAsrc::process no llama a JNI, no aloca, no bloquea).
    jfloat* inPtr = static_cast<jfloat*>(
        env->GetPrimitiveArrayCritical(in, nullptr));
    if (!inPtr) return 0;
    jfloat* outPtr = static_cast<jfloat*>(
        env->GetPrimitiveArrayCritical(out, nullptr));
    if (!outPtr) {
        env->ReleasePrimitiveArrayCritical(in, inPtr, JNI_ABORT);
        return 0;
    }
    const int produced = asrc->process(inPtr, static_cast<int>(inFrames), outPtr,
                                       static_cast<int>(outCap));
    env->ReleasePrimitiveArrayCritical(out, outPtr, 0);
    env->ReleasePrimitiveArrayCritical(in, inPtr, JNI_ABORT);
    return static_cast<jint>(produced);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetClockCorrectionPpm(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jdouble ppm) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->setClockCorrectionPpm(static_cast<double>(ppm));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetSampleRate(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jfloat sampleRateHz) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->setSampleRate(static_cast<float>(sampleRateHz));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetPsiGeometry(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jfloat headRadiusM,
    jfloat distanceM, jfloat azimuthRad) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->setPsiGeometry(static_cast<float>(headRadiusM),
                         static_cast<float>(distanceM),
                         static_cast<float>(azimuthRad));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetPsiAuto(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jboolean autoOn) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->setPsiAuto(autoOn == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetPsiDesignHold(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jboolean hold) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->setPsiDesignHold(hold == JNI_TRUE);
}

JNIEXPORT jdouble JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativePsiEta(
    JNIEnv* env, jobject /*thiz*/, jlong handle) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return 0.5;
    return static_cast<jdouble>(asrc->psiEta());
}

JNIEXPORT jdouble JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativePsiCoherence(
    JNIEnv* env, jobject /*thiz*/, jlong handle) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return 0.5;
    return static_cast<jdouble>(asrc->psiCoherence());
}

// Telemetría en vivo 1 Hz (v2.5.407, IVANNA.Telemetry): puerta Ψ y ppm
// APLICADO por el nativo. Solo lectura de atómicos, sin locks.
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativePsiActive(
    JNIEnv* env, jobject /*thiz*/, jlong handle) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return JNI_FALSE;
    return asrc->psiActive() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jdouble JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeClockCorrectionPpm(
    JNIEnv* env, jobject /*thiz*/, jlong handle) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return 0.0;
    return static_cast<jdouble>(asrc->clockCorrectionPpm());
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetPsiActive(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jboolean active) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->setPsiActive(active == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeSetPsiFir(JNIEnv* env, jobject /*thiz*/,
                                                       jlong handle, jfloatArray firL,
                                                       jfloatArray firR) {
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc || !firL || !firR) return;
    if (env->GetArrayLength(firL) < 64 || env->GetArrayLength(firR) < 64) return;
    jfloat* l = env->GetFloatArrayElements(firL, nullptr);
    if (!l) return;
    jfloat* r = env->GetFloatArrayElements(firR, nullptr);
    if (!r) {
        env->ReleaseFloatArrayElements(firL, l, JNI_ABORT);
        return;
    }
    asrc->setPsiFir(l, r);
    env->ReleaseFloatArrayElements(firR, r, JNI_ABORT);
    env->ReleaseFloatArrayElements(firL, l, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_RouteAAsrc_nativeReset(JNIEnv* env, jobject /*thiz*/,
                                                   jlong handle) {
    (void)env;
    RouteAAsrc* asrc = fromHandle(handle);
    if (!asrc) return;
    asrc->reset();
}

}  // extern "C"
