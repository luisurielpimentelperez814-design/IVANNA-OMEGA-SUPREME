/*
 * ivanna_visualizer_v2_jni.cpp
 * Puente JNI para com.ivanna.omega.visualizer.IvannaVisualizerNativeV2.
 * Carga: System.loadLibrary("ivanna_omega") (mismo .so consolidado que v1).
 *
 * nativeVisV2Create()            → hilo de audio (PlaybackCaptureService), 1 vez
 * nativeVisV2ProcessBlockFromNPE → hilo de audio, con el mono downmix
 *                                   post-NPE (mismo buffer que ya alimenta v1)
 * nativeVisV2Sample              → hilo GL (VisualizerRendererV2), lock-free,
 *                                   devuelve las 13 bandas crudas
 *
 * No reemplaza a ivanna_visualizer_jni.cpp (v1): ambos bridges coexisten,
 * v1 sigue en producción para el shader simple, v2 alimenta wallpaper_v2.glsl.
 *
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 */

#include <jni.h>
#include "../include/audio_thread_priority.h"
#include "../visualizer/gl_uniform_bridge_v2.hpp"

namespace {
inline ivanna::vis::GLUniformBridgeV2* toPtrV2(jlong h) {
    return reinterpret_cast<ivanna::vis::GLUniformBridgeV2*>(static_cast<intptr_t>(h));
}
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Create(
    JNIEnv*, jclass, jfloat sampleRate) {
    auto* bridge = new ivanna::vis::GLUniformBridgeV2();
    bridge->init(sampleRate);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(bridge));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Destroy(
    JNIEnv*, jclass, jlong handle) {
    delete toPtrV2(handle);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Reset(
    JNIEnv*, jclass, jlong handle) {
    if (auto* b = toPtrV2(handle)) b->reset();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2SetDeviceLatency(
    JNIEnv*, jclass, jlong handle, jfloat latencyMs) {
    if (auto* b = toPtrV2(handle)) b->setDeviceLatencyMs(latencyMs);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2ProcessBlockFromNPE(
    JNIEnv* env, jclass, jlong handle, jobject monoBuffer, jint numFrames) {
    ivanna::audio::enableAudioThreadFastMathOnce();
    auto* b = toPtrV2(handle);
    if (!b) return;
    auto* mono = static_cast<float*>(env->GetDirectBufferAddress(monoBuffer));
    if (!mono) return;
    b->processBlockFromNPE(mono, numFrames);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Sample(
    JNIEnv* env, jclass, jlong handle) {
    jfloatArray arr = env->NewFloatArray(ivanna::vis::GTL_BANDS);
    if (!arr) return arr;
    auto* b = toPtrV2(handle);
    float vals[ivanna::vis::GTL_BANDS] = {0.f};
    if (b) {
        b->sampleForRender(vals);
    }
    env->SetFloatArrayRegion(arr, 0, ivanna::vis::GTL_BANDS, vals);
    return arr;
}

} // extern "C"
