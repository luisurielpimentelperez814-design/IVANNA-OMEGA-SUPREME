#pragma once

#include <cstdint>
#include <cstddef>

#if __has_include(<jni.h>)
#include <jni.h>
#else
typedef unsigned char jboolean;
typedef int jint;
typedef long long jlong;
typedef float jfloat;
typedef void* jobject;
typedef void* jfloatArray;
typedef void* JNIEnv;
typedef void* jclass;
#define JNIEXPORT
#define JNICALL
#define JNI_FALSE 0
#define JNI_TRUE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jlong JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeInitEngine(JNIEnv *, jclass);
JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeDestroyEngine(JNIEnv *, jclass, jlong handle);
JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeProcessAudioBlock(JNIEnv *, jclass, jlong handle, jfloatArray leftChannel, jfloatArray rightChannel, jint numSamples);
JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeProcessDirectBuffer(JNIEnv *, jclass, jlong handle, jobject directBuffer, jint numFrames);
JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeGetClassifierProbabilities(JNIEnv *, jclass, jlong handle, jfloatArray outProbs);
JNIEXPORT jint JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeGetDominantClass(JNIEnv *, jclass, jlong handle);
JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeSetGoldenEarMode(JNIEnv *, jclass, jlong handle, jboolean enable);
JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeRunAcousticProfiling(JNIEnv *, jclass, jlong handle);

#ifdef __cplusplus
}
#endif
