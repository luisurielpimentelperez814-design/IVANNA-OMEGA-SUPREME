#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
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
#include "../spatial/fft_radix2.hpp"
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
} // namespace

// FIX BUILD (NDK 25.1): g_safLatentApplied y g_safLatentMutex estaban dentro
// del namespace{} anónimo. En NDK 25.1, instanciar std::unordered_map<jlong,…>
// dentro de un anonymous namespace corrompe el lookup de std::__ndk1::false_type
// en __hash_table → "unknown class name 'false_type'" y crash de build.
// Movidos a file scope con static (internal linkage idéntico). Fix mínimo.
extern "C" bool ivanna_saf_get_latent_snapshot(float out[7]);
static std::unordered_map<jlong, std::array<float,7>> g_safLatentApplied;
static std::mutex g_safLatentAppliedMutex;

// Aplica el latente SAF pendiente al renderer del handle, si cambió.
// Llamada desde nativeObjectRendererCreate (inicial) y renderBlock.
static inline void safApplyPendingToRenderer(jlong handle,
        ivanna::spatial::ObjectRenderer* renderer) {
    float q[7];
    if (!ivanna_saf_get_latent_snapshot(q)) return;  // lectura inconsistente → skip
    {
        std::lock_guard<std::mutex> g(g_safLatentAppliedMutex);
        auto it = g_safLatentApplied.find(handle);
        if (it != g_safLatentApplied.end() &&
            std::memcmp(it->second.data(), q, sizeof(float) * 7) == 0) return;
        std::array<float,7> arr;
        std::memcpy(arr.data(), q, sizeof(float) * 7);
        g_safLatentApplied[handle] = arr;
    }
    renderer->setSafLatent(q, 7);
}
static inline ivanna::ai::NeuralUpmixer* toUpmixer(jlong h) {
    return reinterpret_cast<ivanna::ai::NeuralUpmixer*>(static_cast<intptr_t>(h));
}

// FIX (CI 2026-08-11, exit code 1 en Build Debug APK): el commit anterior
// (6836024) movió el cierre del namespace{} anónimo a la línea 33 para
// sacar g_safLatentApplied/g_safLatentAppliedMutex de ahí, pero dejó el
// cierre ORIGINAL del namespace (que antes cerraba después de toUpmixer,
// más abajo) como una llave huérfana sin apertura correspondiente —
// error de sintaxis directo, confirmado contando llaves línea por línea
// (balance -1 antes de este fix). toUpmixer() marcado 'static' explícito
// para preservar el internal linkage que tenía dentro del namespace
// anónimo (mismo criterio ya aplicado a las otras dos variables movidas).

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
    safApplyPendingToRenderer(reinterpret_cast<jlong>(renderer), renderer);
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
    safApplyPendingToRenderer(handle, renderer);

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

JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSetHrtfSubject(JNIEnv* env, jclass, jlong handle, jstring subjectId) {
    auto* renderer = toObjectRenderer(handle);
    if (!renderer || !subjectId) return;
    const char* subjStr = env->GetStringUTFChars(subjectId, nullptr);
    if (subjStr) {
        // En una implementación completa esto cargaría el sujeto específico
        // del archivo .ihr1 o de un .sofa. Como el modelo SAF maneja
        // hrtf_dataset.ihr1 general, enviamos el comando base.
        // Simulando loadHrtfDatasetFromFile() con la ruta estándar si el
        // módulo magisk monta la base en /data/adb/ivanna_omega/
        renderer->loadHrtfDatasetFromFile("/data/adb/ivanna_omega/hrtf_dataset.ihr1");
        env->ReleaseStringUTFChars(subjectId, subjStr);
    }
}
