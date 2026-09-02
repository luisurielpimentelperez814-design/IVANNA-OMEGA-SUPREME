#include <jni.h>
#include "../visualizer/gl_uniform_bridge_bark64.hpp"

namespace {
inline ivanna::vis::GLUniformBridgeBark64* toBark64Ptr(jlong h) {
    return reinterpret_cast<ivanna::vis::GLUniformBridgeBark64*>(static_cast<intptr_t>(h));
}
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerBark64Native_nativeCreate(
    JNIEnv*, jclass, jfloat sampleRate) {
    auto* bridge = new ivanna::vis::GLUniformBridgeBark64();
    bridge->init(sampleRate);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(bridge));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerBark64Native_nativeDestroy(
    JNIEnv*, jclass, jlong handle) {
    delete toBark64Ptr(handle);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerBark64Native_nativeReset(
    JNIEnv*, jclass, jlong handle) {
    if (auto* b = toBark64Ptr(handle)) b->reset();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerBark64Native_nativeProcessBlock(
    JNIEnv* env, jclass, jlong handle, jobject monoBuffer, jint numFrames) {
    auto* b = toBark64Ptr(handle);
    if (!b) return;
    auto* mono = static_cast<float*>(env->GetDirectBufferAddress(monoBuffer));
    if (!mono) return;
    b->processBlockFromNPE(mono, numFrames);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_visualizer_IvannaVisualizerBark64Native_nativeSampleInto(
    JNIEnv* env, jclass, jlong handle, jfloatArray dst) {
    auto* b = toBark64Ptr(handle);
    if (!b || !dst) return;
    jfloat* dstPtr = env->GetFloatArrayElements(dst, nullptr);
    if (!dstPtr) return;
    b->sampleForRender(dstPtr);
    env->ReleaseFloatArrayElements(dst, dstPtr, 0);
}

} // extern "C"
