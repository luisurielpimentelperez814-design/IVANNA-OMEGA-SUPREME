/*
 * ivanna_visualizer_jni.cpp
 * Puente JNI para com.ivanna.omega.visualizer.IvannaVisualizerNative.
 * Carga: System.loadLibrary("ivanna_visualizer")
 *
 * nativeVisCreate()      → hilo de audio (PlaybackCaptureService), 1 vez
 * nativeVisProcessBlock  → hilo de audio, por cada bloque capturado
 * nativeVisSample        → hilo GL (Renderer.onDrawFrame), lock-free
 *
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 */

#include <jni.h>
#include "../include/audio_thread_priority.h"
#include "../visualizer/gl_uniform_bridge.hpp"

namespace {
inline ivanna::vis::GLUniformBridge* toPtr(jlong h) {
    return reinterpret_cast<ivanna::vis::GLUniformBridge*>(static_cast<intptr_t>(h));
}
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisCreate(
    JNIEnv*, jclass, jfloat sampleRate) {
    auto* bridge = new ivanna::vis::GLUniformBridge();
    bridge->init(sampleRate);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(bridge));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisDestroy(
    JNIEnv*, jclass, jlong handle) {
    delete toPtr(handle);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisReset(
    JNIEnv*, jclass, jlong handle) {
    if (auto* b = toPtr(handle)) b->reset();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisSetDeviceLatency(
    JNIEnv*, jclass, jlong handle, jfloat latencyMs) {
    if (auto* b = toPtr(handle)) b->setDeviceLatencyMs(latencyMs);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisProcessBlock(
    JNIEnv* env, jclass, jlong handle, jobject monoBuffer, jint numFrames) {
    ivanna::audio::enableAudioThreadFastMathOnce();
    auto* b = toPtr(handle);
    if (!b) return;
    auto* mono = static_cast<float*>(env->GetDirectBufferAddress(monoBuffer));
    if (!mono) return;
    b->processBlock(mono, numFrames);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisSample(
    JNIEnv* env, jclass, jlong handle) {
    jfloatArray arr = env->NewFloatArray(3);
    if (!arr) return arr;
    auto* b = toPtr(handle);
    float vals[3] = {0.f, 0.f, 0.f};
    if (b) {
        const auto u = b->sampleForRender();
        vals[0] = u.bass_pulse; vals[1] = u.mid_flow; vals[2] = u.high_flicker;
    }
    env->SetFloatArrayRegion(arr, 0, 3, vals);
    return arr;
}

} // extern "C"
