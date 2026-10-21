/*
 * ivanna_predictive_jni.cpp — puente JNI del motor predictivo (Módulo 1).
 * Implementa com.ivanna.omega.audio.PredictiveAudioNative.
 *
 * Observador de SOLO LECTURA: analiza el audio que el reproductor ya produce y
 * publica telemetría (confianza, ganancia de predicción, transitorio). No
 * modifica el audio. Cada handle pertenece a UN hilo (el del decode loop).
 *
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 */
#include <jni.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>

#include "../predictive_audio_engine.hpp"

namespace {

constexpr int kScratch = 1024;
constexpr int kFrameFields = 9;

struct Handle {
    ivanna::PredictiveAudioEngine engine;
    std::array<float, kScratch> l{};
    std::array<float, kScratch> r{};
};

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_audio_PredictiveAudioNative_nativeCreate(JNIEnv*, jclass, jfloat sampleRate) {
    if (!(sampleRate >= 8000.f && sampleRate <= 768000.f)) return 0;
    auto* h = new (std::nothrow) Handle();
    if (!h) return 0;
    h->engine.prepare(sampleRate);
    return reinterpret_cast<jlong>(h);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_PredictiveAudioNative_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete reinterpret_cast<Handle*>(handle);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_PredictiveAudioNative_nativeReset(JNIEnv*, jclass, jlong handle) {
    if (auto* h = reinterpret_cast<Handle*>(handle)) h->engine.reset();
}

// `interleaved` = L,R,L,R… ; procesa `frames` frames. false si los argumentos no son válidos.
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_audio_PredictiveAudioNative_nativeProcessInterleaved(
    JNIEnv* env, jclass, jlong handle, jfloatArray interleaved, jint frames) {
    auto* h = reinterpret_cast<Handle*>(handle);
    if (!h || !interleaved || frames < 0) return JNI_FALSE;
    if (static_cast<jlong>(env->GetArrayLength(interleaved)) < static_cast<jlong>(frames) * 2) return JNI_FALSE;
    jfloat* data = env->GetFloatArrayElements(interleaved, nullptr);
    if (!data) return JNI_FALSE;
    for (int pos = 0; pos < frames; pos += kScratch) {
        const int n = std::min(kScratch, frames - pos);
        for (int i = 0; i < n; ++i) {
            h->l[static_cast<size_t>(i)] = data[(pos + i) * 2];
            h->r[static_cast<size_t>(i)] = data[(pos + i) * 2 + 1];
        }
        h->engine.processStereo(h->l.data(), h->r.data(), n);
    }
    env->ReleaseFloatArrayElements(interleaved, data, JNI_ABORT);  // solo lectura
    return JNI_TRUE;
}

// out[9] = valid, confidence, nmseNear, nmseFar, predictionGainDb, predictedPeak,
//          predictedSlewPerMs, zeroCrossHz, transient (1/0).
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_audio_PredictiveAudioNative_nativeGetFrame(
    JNIEnv* env, jclass, jlong handle, jfloatArray out) {
    auto* h = reinterpret_cast<Handle*>(handle);
    if (!h || !out || env->GetArrayLength(out) < kFrameFields) return JNI_FALSE;
    const auto& f = h->engine.frame();
    const jfloat v[kFrameFields] = {
        f.valid ? 1.f : 0.f, f.confidence, f.nmseNear, f.nmseFar, f.predictionGainDb,
        f.predictedPeak, f.predictedSlewPerMs, f.zeroCrossHz, f.transient ? 1.f : 0.f};
    env->SetFloatArrayRegion(out, 0, kFrameFields, v);
    return JNI_TRUE;
}

}  // extern "C"
