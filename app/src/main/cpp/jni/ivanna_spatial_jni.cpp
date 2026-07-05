// ivanna_spatial_jni.cpp
// ============================================================================
// IVANNA — JNI Bridge para Spatial Audio (Head Tracking + Object Renderer)
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [MAJESTY-JNI-1.0] Puente entre el motor de audio espacial en C++ y la
// capa de Android (Kotlin). Expone:
//   - HeadTracker: recibe datos del sensor IMU
//   - ObjectRenderer: renderizado de objetos 3D
//   - NeuralUpmixer: separación AI de stems
// ============================================================================

#include <jni.h>
#include "../spatial/ivanna_head_tracker.hpp"
#include "../spatial/ivanna_object_renderer.hpp"
#include "../neuromorphic/ivanna_neural_upmixer.hpp"
#include "../include/audio_thread_priority.h"

namespace {
inline ivanna::spatial::HeadTracker* toHeadTracker(jlong h) {
    return reinterpret_cast<ivanna::spatial::HeadTracker*>(static_cast<intptr_t>(h));
}
inline ivanna::spatial::ObjectRenderer* toObjectRenderer(jlong h) {
    return reinterpret_cast<ivanna::spatial::ObjectRenderer*>(static_cast<intptr_t>(h));
}
inline ivanna::ai::NeuralUpmixer* toUpmixer(jlong h) {
    return reinterpret_cast<ivanna::ai::NeuralUpmixer*>(static_cast<intptr_t>(h));
}
}

// ============================================================================
// HeadTracker JNI
// ============================================================================
extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeHeadTrackerCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new ivanna::spatial::HeadTracker());
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeHeadTrackerDestroy(JNIEnv*, jclass, jlong handle) {
    delete toHeadTracker(handle);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeHeadTrackerUpdate(
    JNIEnv*, jclass, jlong handle, jfloat x, jfloat y, jfloat z, jfloat w, jfloat timestampMs) {
    auto* tracker = toHeadTracker(handle);
    if (!tracker) return;
    float rv[4] = {x, y, z, w};
    tracker->update(rv, timestampMs);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeHeadTrackerReset(JNIEnv*, jclass, jlong handle) {
    auto* tracker = toHeadTracker(handle);
    if (tracker) tracker->reset();
}

// ============================================================================
// ObjectRenderer JNI
// ============================================================================

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererCreate(
    JNIEnv*, jclass, jfloat sampleRate, jint blockSize) {
    auto* renderer = new ivanna::spatial::ObjectRenderer();
    renderer->init(sampleRate, blockSize);
    return reinterpret_cast<jlong>(renderer);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererDestroy(JNIEnv*, jclass, jlong handle) {
    delete toObjectRenderer(handle);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSetHeadTracker(
    JNIEnv*, jclass, jlong rendererHandle, jlong trackerHandle) {
    auto* renderer = toObjectRenderer(rendererHandle);
    auto* tracker = toHeadTracker(trackerHandle);
    if (renderer && tracker) renderer->setHeadTracker(tracker);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSetReverb(
    JNIEnv*, jclass, jlong handle, jfloat level) {
    auto* renderer = toObjectRenderer(handle);
    if (renderer) renderer->setReverbLevel(level);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererRenderBlock(
    JNIEnv* env, jclass, jlong handle, jobject objectsBuffer, jint numObjects,
    jobject outLeftBuffer, jobject outRightBuffer, jint numFrames) {
    ivanna::audio::enableAudioThreadFastMathOnce();
    auto* renderer = toObjectRenderer(handle);
    if (!renderer) return;

    auto* objectsIn = static_cast<float*>(env->GetDirectBufferAddress(objectsBuffer));
    auto* outL = static_cast<float*>(env->GetDirectBufferAddress(outLeftBuffer));
    auto* outR = static_cast<float*>(env->GetDirectBufferAddress(outRightBuffer));
    if (!objectsIn || !outL || !outR) return;

    renderer->renderBlock(objectsIn, numObjects, outL, outR, numFrames);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererReset(JNIEnv*, jclass, jlong handle) {
    auto* renderer = toObjectRenderer(handle);
    if (renderer) renderer->reset();
}

// [FIX-SILENCE] Puentea las posiciones de stem del upmixer (kStemPositions
// o customPositions_ tras setStemPosition) hacia la lista de objetos
// activos del renderer. stemsToObjects() ignora el puntero/numFrames de
// audio que recibe (solo usa las posiciones), así que se puede invocar
// con nullptr/0 de forma segura únicamente para (re)generar la lista.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSyncStemObjects(
    JNIEnv*, jclass, jlong rendererHandle, jlong upmixerHandle) {
    auto* renderer = toObjectRenderer(rendererHandle);
    auto* upmixer = toUpmixer(upmixerHandle);
    if (!renderer || !upmixer) return;

    std::vector<ivanna::spatial::AudioObject> objects;
    upmixer->stemsToObjects(nullptr, 0, objects);
    renderer->setObjects(objects);
}

// ============================================================================
// NeuralUpmixer JNI
// ============================================================================

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeUpmixerCreate(
    JNIEnv* env, jclass, jstring modelPath, jfloat sampleRate, jint blockSize) {
    auto* upmixer = new ivanna::ai::NeuralUpmixer();
    const char* path = env->GetStringUTFChars(modelPath, nullptr);
    bool ok = upmixer->init(sampleRate, blockSize);
    env->ReleaseStringUTFChars(modelPath, path);
    if (!ok) {
        delete upmixer;
        return 0;
    }
    return reinterpret_cast<jlong>(upmixer);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeUpmixerDestroy(JNIEnv*, jclass, jlong handle) {
    auto* upmixer = toUpmixer(handle);
    if (upmixer) {
        upmixer->release();
        delete upmixer;
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeUpmixerProcess(
    JNIEnv* env, jclass, jlong handle, jobject inBuffer, jobject outBuffer, jint numFrames) {
    auto* upmixer = toUpmixer(handle);
    if (!upmixer) return;

    auto* in = static_cast<float*>(env->GetDirectBufferAddress(inBuffer));
    auto* out = static_cast<float*>(env->GetDirectBufferAddress(outBuffer));
    if (!in || !out) return;

    upmixer->process(in, out, numFrames);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeUpmixerSetEnabled(
    JNIEnv*, jclass, jlong handle, jboolean enabled) {
    auto* upmixer = toUpmixer(handle);
    if (upmixer) upmixer->setEnabled(enabled);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeUpmixerSetStemPosition(
    JNIEnv*, jclass, jlong handle, jint stemType, jfloat x, jfloat y, jfloat z, jfloat width) {
    auto* upmixer = toUpmixer(handle);
    if (upmixer) {
        upmixer->setStemPosition(static_cast<ivanna::ai::StemType>(stemType), x, y, z, width);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeUpmixerReset(JNIEnv*, jclass, jlong handle) {
    auto* upmixer = toUpmixer(handle);
    if (upmixer) upmixer->reset();
}

} // extern "C"
