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
#include "../spatial/IvannaAudioPipeline.hpp"
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
extern "C" uint32_t ivanna_saf_get_latent_seq();
extern "C" void ivanna_saf_apply_latent(const float q[7]);
static std::atomic<uint32_t> g_lastObservedSafSeq{0};
static std::atomic<jlong>    g_lastObservedHandle{0};
static std::unordered_map<jlong, std::array<float,7>> g_safLatentApplied;
static std::mutex g_safLatentAppliedMutex;

// Aplica el latente SAF pendiente al renderer del handle, si cambió.
// Fast-path 100% lock-free en el hilo de audio: si el contador de secuencia
// atómico global y el handle activo no cambiaron, retorna en 2 cargas atómicas
// sin tocar std::mutex ni std::unordered_map.
static inline void safApplyPendingToRenderer(jlong handle,
        ivanna::spatial::ObjectRenderer* renderer) {
    const uint32_t curSeq = ivanna_saf_get_latent_seq();
    if ((curSeq & 1u) != 0u) return; // escritura en curso
    if (curSeq == g_lastObservedSafSeq.load(std::memory_order_relaxed) &&
        handle == g_lastObservedHandle.load(std::memory_order_relaxed)) {
        return;
    }
    float q[7];
    if (!ivanna_saf_get_latent_snapshot(q)) return;  // lectura inconsistente → skip
    {
        std::lock_guard<std::mutex> g(g_safLatentAppliedMutex);
        auto it = g_safLatentApplied.find(handle);
        if (it != g_safLatentApplied.end() &&
            std::memcmp(it->second.data(), q, sizeof(float) * 7) == 0) {
            g_lastObservedSafSeq.store(curSeq, std::memory_order_relaxed);
            g_lastObservedHandle.store(handle, std::memory_order_relaxed);
            return;
        }
        std::array<float,7> arr;
        std::memcpy(arr.data(), q, sizeof(float) * 7);
        g_safLatentApplied[handle] = arr;
    }
    g_lastObservedSafSeq.store(curSeq, std::memory_order_relaxed);
    g_lastObservedHandle.store(handle, std::memory_order_relaxed);
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



extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSetHrtfSubject(JNIEnv* env, jclass, jlong handle, jstring subjectId) {
    auto* renderer = toObjectRenderer(handle);
    if (!renderer || !subjectId) return;
    const char* subjStr = env->GetStringUTFChars(subjectId, nullptr);
    if (subjStr) {
        const std::string subj(subjStr);
        env->ReleaseStringUTFChars(subjectId, subjStr);

        // Rutas en orden de prioridad:
        // 1. Módulo Magisk montado en /system/etc (disponible con root)
        // 2. Ruta legado /data/adb
        //
        // FIX (UB grave — heap-use-after-free): la versión anterior guardaba
        // en `candidates[]` los `.c_str()` de temporales `std::string`
        // construidos en la expresión de inicialización. Los temporales se
        // destruyen al final de esa expresión, así que cada `candidates[i]`
        // apuntaba a memoria ya liberada — comportamiento indefinido:
        // a veces "funciona" (heap aún sin reescribir), a veces lee basura
        // y loadHrtfDatasetFromFile abre rutas corruptas o crashea. Los
        // std::string ahora viven en variables con nombre hasta el final
        // del scope, y el array solo guarda vistas válidas.
        const std::string candSystemEtc  = "/system/etc/ivanna_omega/hrtf/" + subj + ".ihr1";
        const std::string candDataAdb    = "/data/adb/ivanna_omega/hrtf/" + subj + ".ihr1";
        const std::string candNoRootData = "/data/data/com.ivanna.omega/files/ivanna_omega/hrtf/" + subj + ".ihr1";
        const std::string candNoRootUser = "/data/user/0/com.ivanna.omega/files/ivanna_omega/hrtf/" + subj + ".ihr1";
        const std::string candLegacy     = "/data/adb/ivanna_omega/hrtf_" + subj + ".ihr1";
        const char* candidates[] = {
            candSystemEtc.c_str(),
            candDataAdb.c_str(),
            candNoRootData.c_str(),
            candNoRootUser.c_str(),
            candLegacy.c_str(),
            nullptr
        };
        for (int i = 0; candidates[i]; ++i) {
            if (renderer->loadHrtfDatasetFromFile(candidates[i])) {
                renderer->setCurrentSubjectName(subj.c_str());
                break;
            }
        }
        // Acoplar el ancla SAF entrenada sobre SOFA/IHR1 para este sujeto con el prior Golden Master
        for (size_t s = 0; s < ivanna::master::kNumTrainedSubjects; ++s) {
            if (subj == ivanna::master::kTrainedSubjectAnchors[s].id) {
                float qCoupled[7];
                for (int k = 0; k < 7; ++k) {
                    qCoupled[k] = 0.65f * ivanna::master::kMasterSafGoldenQ[k]
                                + 0.35f * ivanna::master::kTrainedSubjectAnchors[s].q[k];
                }
                ivanna_saf_apply_latent(qCoupled);
                renderer->setSafLatent(qCoupled, 7);
                break;
            }
        }
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererIsHrtfLoaded(JNIEnv*, jclass, jlong handle) {
    auto* renderer = toObjectRenderer(handle);
    return (renderer && renderer->hrtfDatasetLoaded()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererGetCurrentSubject(JNIEnv* env, jclass, jlong handle) {
    auto* renderer = toObjectRenderer(handle);
    if (!renderer) return env->NewStringUTF("none");
    return env->NewStringUTF(renderer->currentSubject().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSetAutoEqEnabled(JNIEnv* env, jclass, jlong handle, jboolean enabled) {
    auto* renderer = toObjectRenderer(handle);
    if (renderer) {
        renderer->getAutoEq().setEnabled(enabled);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeObjectRendererSetAutoEqBand(JNIEnv* env, jclass, jlong handle, jint bandIndex, jfloat freqHz, jfloat gainDb, jfloat q) {
    auto* renderer = toObjectRenderer(handle);
    if (renderer) {
        renderer->getAutoEq().setBand(bandIndex, freqHz, gainDb, q);
    }
}

// ============================================================================
// Eje Supremo Neuroacústico — Inversión Biomecánica Coclear Activa (Cochlear-PINN)
// Control seguro lock-free y sin contención con hilos SCHED_FIFO.
//
// Los 3 símbolos JNI (nativeSet*/isCochlear*) residen AQUÍ (ivanna_spatial_jni.cpp)
// como única definición para todo el .so. Los atomics g_cochlearEnabled /
// g_cochlearIntensity están definidos en ivanna_omega_jni.cpp con enlace externo
// y se declaran aquí vía extern para que los helpers los actualicen también —
// el hot-path de nativeProcess / nativeProcessBlock los lee directamente.
// ============================================================================
#include "../neuromorphic/CochlearActiveInverseModel.hpp"
extern ivanna::neuromorphic::CochlearActiveInverseEngine g_cochlearEngine;
extern std::atomic<bool>  g_cochlearEnabled;
extern std::atomic<float> g_cochlearIntensity;

static inline void cochlearSetEnabledHelper(jboolean enabled) noexcept {
    const bool on = (enabled == JNI_TRUE);
    // Ruta A: pipeline IvannaAudioPipeline (gestiona su propio engine interno)
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().cochlearEngine().setEnabled(on);
    // Ruta B / hot-path DSPBridge & IvannaNativeLib: sincronizar g_cochlearEngine + atomic
    g_cochlearEngine.setEnabled(on);
    g_cochlearEnabled.store(on, std::memory_order_release);
}

static inline void cochlearSetIntensityHelper(jfloat intensity) noexcept {
    const float w = std::isfinite(static_cast<float>(intensity))
        ? (intensity < 0.f ? 0.f : (intensity > 1.f ? 1.f : static_cast<float>(intensity)))
        : 0.35f;
    // Ruta A: pipeline
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().cochlearEngine().setIntensity(w);
    // Ruta B / hot-path manual: sincronizar g_cochlearEngine + atomic
    g_cochlearEngine.setIntensity(w);
    g_cochlearIntensity.store(w, std::memory_order_relaxed);
}

static inline jboolean cochlearIsActiveHelper() noexcept {
    const bool active = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().cochlearEngine().isActive()
                     || g_cochlearEngine.isActive();
    return active ? JNI_TRUE : JNI_FALSE;
}

static inline jfloat cochlearGetIntensityHelper() noexcept {
    return static_cast<jfloat>(g_cochlearIntensity.load(std::memory_order_relaxed));
}

// ── IvannaSpatialNative bindings ────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_setCochlearInverseEnabled(JNIEnv*, jclass, jboolean enabled) {
    cochlearSetEnabledHelper(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeSetCochlearInverseEnabled(JNIEnv*, jclass, jboolean enabled) {
    cochlearSetEnabledHelper(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_setCochlearIntensity(JNIEnv*, jclass, jfloat intensity) {
    cochlearSetIntensityHelper(intensity);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeSetCochlearIntensity(JNIEnv*, jclass, jfloat intensity) {
    cochlearSetIntensityHelper(intensity);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_isCochlearActive(JNIEnv*, jclass) {
    return cochlearIsActiveHelper();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeIsCochlearActive(JNIEnv*, jclass) {
    return cochlearIsActiveHelper();
}

// ── IvannaNativeLib bindings ────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_setCochlearInverseEnabled(JNIEnv*, jobject, jboolean enabled) {
    cochlearSetEnabledHelper(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCochlearInverseEnabled(JNIEnv*, jobject, jboolean enabled) {
    cochlearSetEnabledHelper(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_setCochlearIntensity(JNIEnv*, jobject, jfloat intensity) {
    cochlearSetIntensityHelper(intensity);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetCochlearIntensity(JNIEnv*, jobject, jfloat intensity) {
    cochlearSetIntensityHelper(intensity);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_isCochlearActive(JNIEnv*, jobject) {
    return cochlearIsActiveHelper();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsCochlearActive(JNIEnv*, jobject) {
    return cochlearIsActiveHelper();
}

// ── NativeBridge bindings ───────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setCochlearInverseEnabled(JNIEnv*, jclass, jboolean enabled) {
    cochlearSetEnabledHelper(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setCochlearIntensity(JNIEnv*, jclass, jfloat intensity) {
    cochlearSetIntensityHelper(intensity);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_NativeBridge_isCochlearActive(JNIEnv*, jclass) {
    return cochlearIsActiveHelper();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_NativeBridge_getCochlearIntensity(JNIEnv*, jclass) {
    return cochlearGetIntensityHelper();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetCochlearIntensity(JNIEnv*, jobject) {
    return cochlearGetIntensityHelper();
}

// ============================================================================
// 5 EJES DE SUPREMACÍA NEUROACÚSTICA — JNI Bindings (NativeBridge & IvannaNativeLib)
// ============================================================================

// ── EJE 1: WarpedLatticeTransducerInverter ──────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setWarpedLatticeEnabled(JNIEnv*, jclass, jboolean en) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().setEnabled(en == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setWarpedLatticeMicroChirp(JNIEnv*, jclass, jboolean en) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().setMicroChirpEnabled(en == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setWarpedLatticeBlDrive(JNIEnv*, jclass, jfloat drive) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().setBlCompensationDrive(drive);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setWarpedLatticeLambda(JNIEnv*, jclass, jfloat lam) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().setWarpingLambda(lam);
}
extern "C" uint32_t ivanna_get_routeB_declipped_peaks() noexcept;
extern "C" float    ivanna_get_routeB_tape_mag() noexcept;
extern "C" uint32_t ivanna_get_routeB_snn_spikes() noexcept;
extern "C" float    ivanna_get_routeB_subsample_delay() noexcept;
extern "C" bool     ivanna_is_routeB_active() noexcept;
extern "C" void     ivanna_set_route_rir_xtc(int routeIdx) noexcept;

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_NativeBridge_getWarpedLatticeSubSampleDelay(JNIEnv*, jclass) {
    if (ivanna_is_routeB_active()) {
        return ivanna_get_routeB_subsample_delay();
    }
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().lastSubSampleDelay();
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_runWarpedLatticeLoopbackCalibration(JNIEnv*, jclass, jfloat f0Hz, jfloat mu) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().runSyntheticLoopbackCalibration(f0Hz, mu);
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_NativeBridge_getWarpedLatticeKappas(JNIEnv* env, jclass) {
    jfloatArray arr = env->NewFloatArray(8);
    if (!arr) return nullptr;
    float tmp[8]{};
    const auto& inv = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter();
    for (size_t i = 0; i < 8; ++i) tmp[i] = inv.reflectionCoefficient(i);
    env->SetFloatArrayRegion(arr, 0, 8, tmp);
    return arr;
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setWarpedLatticeRouteArchetype(JNIEnv*, jclass, jint routeIdx) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().setRouteArchetype(routeIdx);
    ivanna_set_route_rir_xtc(routeIdx);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_NativeBridge_getWarpedLatticeDeclippedPeaks(JNIEnv*, jclass) {
    const uint32_t localPeaks = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().warpedLatticeInverter().declippedPeaksCount();
    const uint32_t routeBPeaks = ivanna_get_routeB_declipped_peaks();
    return static_cast<jint>(std::max(localPeaks, routeBPeaks));
}

// ── EJE 2: PhaseCoherentTransharmonicSynthesizer ────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setTransharmonicCvnnEnabled(JNIEnv*, jclass, jboolean en) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().transharmonicSynth().setEnabled(en == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setTransharmonicHarmonicGain(JNIEnv*, jclass, jfloat gain) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().transharmonicSynth().setHarmonicGain(gain);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setTransharmonicImdCancel(JNIEnv*, jclass, jfloat strength) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().transharmonicSynth().setImdCancelStrength(strength);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setTransharmonicAnalogTapeDrive(JNIEnv*, jclass, jfloat drive) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().transharmonicSynth().setAnalogTapeDrive(drive);
}
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_NativeBridge_getTransharmonicPhaseStep(JNIEnv*, jclass) {
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance().transharmonicSynth().maxPhaseDerivativeStep();
}
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_NativeBridge_getTransharmonicTapeMagnetization(JNIEnv*, jclass) {
    const float localMag = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().transharmonicSynth().lastTapeMagnetization();
    if (ivanna_is_routeB_active() && std::fabs(localMag) < 1e-6f) {
        return ivanna_get_routeB_tape_mag();
    }
    return localMag;
}

// ── EJE 3: SnnNmfHoaUpmixer ─────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setSnnHoaUpmixerEnabled(JNIEnv*, jclass, jboolean en) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().snnNmfHoaUpmixer().setEnabled(en == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setSnnHoaImmersivity(JNIEnv*, jclass, jfloat w) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().snnNmfHoaUpmixer().setImmersivity(w);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setSnnSpikeThreshold(JNIEnv*, jclass, jfloat th) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().snnNmfHoaUpmixer().setSnnThreshold(th);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_NativeBridge_promoteSnnThreadToSchedFifo(JNIEnv*, jclass, jint prio) {
    return ivanna::supreme::SnnNmfHoaUpmixer::promoteCurrentThreadToSchedFifo(prio) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_NativeBridge_getSnnActiveSpikes(JNIEnv*, jclass) {
    if (ivanna_is_routeB_active()) {
        return static_cast<jint>(ivanna_get_routeB_snn_spikes());
    }
    return static_cast<jint>(ivanna::spatial::IvannaAudioPipeline::getActiveInstance().snnNmfHoaUpmixer().lastActiveSpikes());
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_NativeBridge_getSnnOrthogonalMasks(JNIEnv* env, jclass) {
    jfloatArray arr = env->NewFloatArray(4);
    if (!arr) return nullptr;
    float tmp[4]{};
    const auto& up = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().snnNmfHoaUpmixer();
    for (size_t k = 0; k < 4; ++k) tmp[k] = up.streamMask(k);
    env->SetFloatArrayRegion(arr, 0, 4, tmp);
    return arr;
}

// ── EJE 4: PinnaManifoldInterpolator ────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setPinnaManifoldEnabled(JNIEnv*, jclass, jboolean en) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator().setEnabled(en == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setPinnaManifoldWetMix(JNIEnv*, jclass, jfloat wet) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator().setWetMix(wet);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_calibratePinnaManifold(
    JNIEnv*, jclass, jfloat conchaDepth, jfloat helixCurl, jfloat headWidth) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance()
        .pinnaManifoldInterpolator()
        .calibrateFromLatents(conchaDepth, helixCurl, headWidth, 48000.0f);
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_NativeBridge_calibratePinnaFromImagePatch(
    JNIEnv* env, jclass, jfloatArray patchArr) {
    auto& pinna = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator();
    if (patchArr) {
        const jsize len = env->GetArrayLength(patchArr);
        if (len > 0) {
            std::vector<float> patch(static_cast<size_t>(len), 0.0f);
            env->GetFloatArrayRegion(patchArr, 0, len, patch.data());
            pinna.calibrateFromImagePatch(patch.data(), patch.size(), 48000.0f);
        }
    }
    jfloatArray outLatents = env->NewFloatArray(6);
    if (!outLatents) return nullptr;
    float lat[6]{};
    for (size_t i = 0; i < 6; ++i) lat[i] = pinna.activeLatent(i);
    env->SetFloatArrayRegion(outLatents, 0, 6, lat);
    return outLatents;
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_NativeBridge_getPinnaActiveLatents(JNIEnv* env, jclass) {
    jfloatArray outLatents = env->NewFloatArray(6);
    if (!outLatents) return nullptr;
    float lat[6]{};
    const auto& pinna = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator();
    for (size_t i = 0; i < 6; ++i) lat[i] = pinna.activeLatent(i);
    env->SetFloatArrayRegion(outLatents, 0, 6, lat);
    return outLatents;
}
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_NativeBridge_getPinnaActiveNotchHz(JNIEnv*, jclass) {
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator().activeNotchFreqHz();
}
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_NativeBridge_getPinnaActiveItdUs(JNIEnv*, jclass) {
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator().activeItdMicroSeconds();
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_NativeBridge_getPinnaActiveFirTaps(JNIEnv* env, jclass) {
    jfloatArray outTaps = env->NewFloatArray(8);
    if (!outTaps) return nullptr;
    float taps[8]{};
    const auto& pinna = ivanna::spatial::IvannaAudioPipeline::getActiveInstance().pinnaManifoldInterpolator();
    for (size_t i = 0; i < 8; ++i) taps[i] = pinna.activeFirLeft(i);
    env->SetFloatArrayRegion(outTaps, 0, 8, taps);
    return outTaps;
}

// ── EJE 5: SupremeMsoFarrowArbitrator ───────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setFarrowMsoEnabled(JNIEnv*, jclass, jboolean en) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().shmMsoArbitrator().setEnabled(en == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setMsoItdNanoseconds(JNIEnv*, jclass, jfloat ns) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().shmMsoArbitrator().setMsoItdNanoseconds(ns);
}
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setEbpfBypassActive(JNIEnv*, jclass, jboolean active) {
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().shmMsoArbitrator().setEbpfBypassActive(active == JNI_TRUE);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_NativeBridge_acquireShmArbitration(JNIEnv*, jclass, jint pid) {
    const auto nowTp = std::chrono::steady_clock::now().time_since_epoch();
    const uint64_t nowNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(nowTp).count());
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance()
        .shmMsoArbitrator().tryAcquireOwnership(pid, nowNs > 0 ? nowNs : 100'000'000ULL) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_NativeBridge_releaseShmArbitration(JNIEnv*, jclass, jint pid) {
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance()
        .shmMsoArbitrator().releaseOwnership(pid) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_NativeBridge_getShmOwnerPid(JNIEnv*, jclass) {
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance().shmMsoArbitrator().ownerPid();
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_NativeBridge_isShmCrossProcessMapped(JNIEnv*, jclass) {
    return ivanna::spatial::IvannaAudioPipeline::getActiveInstance()
        .shmMsoArbitrator().isCrossProcessShmMapped() ? JNI_TRUE : JNI_FALSE;
}

// ── ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1–8) ─────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setRealityReconstructionEnabled(JNIEnv*, jclass, jboolean en) {
    const bool enabled = (en == JNI_TRUE);
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().setRealityReconstructionEnabled(enabled);
    ivanna::reality::AcousticRealityOrchestrator::instance().setEnabled(enabled);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_NativeBridge_isRealityReconstructionEnabled(JNIEnv*, jclass) {
    return ivanna::reality::AcousticRealityOrchestrator::instance().isEnabled() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setRealityIntensity(JNIEnv*, jclass, jfloat intensity) {
    ivanna::reality::AcousticRealityOrchestrator::instance().setRealityIntensity(intensity);
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().realityOrchestrator().setRealityIntensity(intensity);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_NativeBridge_setPersonalAuditoryProfile(
    JNIEnv*, jclass, jfloat headRadiusM, jfloat pinnaDepthM,
    jfloat elevationBiasDeg, jint transducerType, jfloat sensitivityScore) {
    ivanna::reality::ListenerRealityInput in{};
    in.pinnaProfile.head_circumference_cm = std::clamp(headRadiusM * 100.0f * 2.0f * 3.14159265f, 48.0f, 64.0f);
    in.pinnaProfile.ear_pinna_size_mm     = std::clamp(pinnaDepthM * 2000.0f, 50.0f, 80.0f);
    in.pinnaProfile.canal_resonance_boost_db = std::clamp((sensitivityScore - 1.0f) * 6.0f, -6.0f, 6.0f);
    in.headPitchDeg = std::clamp(elevationBiasDeg, -25.0f, 25.0f);
    in.deviceClass  = static_cast<ivanna::reality::DeviceTransducerClass>(std::clamp(static_cast<int>(transducerType), 0, 4));
    ivanna::reality::AcousticRealityOrchestrator::instance().personalModel().setListenerInput(in);
    ivanna::spatial::IvannaAudioPipeline::getActiveInstance().realityOrchestrator().personalModel().setListenerInput(in);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_NativeBridge_getRealityTelemetrySnapshot(JNIEnv* env, jclass) {
    jfloatArray outArr = env->NewFloatArray(16);
    if (!outArr) return nullptr;

    auto& orch = ivanna::reality::AcousticRealityOrchestrator::instance();
    auto snap = orch.stateBus().readLatestSnapshot();
    if (snap.sequence == 0) {
        ivanna::experimental::RawAudioMetrics seedM{};
        seedM.rms              = 0.22f;
        seedM.peak             = 0.68f;
        seedM.crest_factor_db  = 9.8f;
        seedM.band_low_energy  = 0.08f;
        seedM.band_mid_energy  = 0.11f;
        seedM.band_high_energy = 0.04f;
        seedM.voice_score      = 0.64f;
        const auto seedAdapt = ivanna::experimental::AdaptiveDecisionEngine::evaluate(seedM, 0.12f, 0.08f);
        std::array<ivanna::spatial::DecomposedObject, 4> seedObjs{};
        seedObjs[0].position = { 0.0f, 1.45f, 0.05f }; seedObjs[0].energy = 0.11f; seedObjs[0].gain = 1.0f;
        seedObjs[1].position = {-0.58f, 2.20f, 0.18f }; seedObjs[1].energy = 0.05f; seedObjs[1].gain = 0.8f;
        seedObjs[2].position = { 0.58f, 2.20f, 0.18f }; seedObjs[2].energy = 0.05f; seedObjs[2].gain = 0.8f;
        seedObjs[3].position = { 0.0f, 1.70f, -0.18f }; seedObjs[3].energy = 0.08f; seedObjs[3].gain = 0.7f;
        snap = orch.orchestrateCycle(seedM, seedAdapt, seedObjs, 0.42f, 0.34f, 48000.0f, 0.020f, 20000ULL);
    }

    float t[16]{};
    t[0]  = snap.perceptual.scores.presence;
    t[1]  = snap.perceptual.scores.naturalness;
    t[2]  = snap.perceptual.scores.separation;
    t[3]  = 1.0f - snap.perceptual.scores.fatigueFree;
    t[4]  = snap.perceptual.scores.immersion;
    t[5]  = snap.perceptual.scores.realismScore;
    t[6]  = snap.neuralProposal.inferredRoomDimsMeters[0];
    t[7]  = snap.neuralProposal.inferredRoomDimsMeters[1];
    t[8]  = snap.neuralProposal.inferredRoomDimsMeters[2];
    t[9]  = snap.genome.roomFingerprint.estimatedRt60Sec;
    t[10] = snap.genome.roomFingerprint.earlyToLateRatioDb;
    t[11] = 1.0f + snap.microMap.intelligibilityContrast;
    t[12] = snap.microMap.nodes[0].existence; // MicroTransients
    t[13] = snap.microMap.nodes[2].existence; // HumanBreath
    t[14] = snap.microMap.nodes[4].existence; // RoomAir
    t[15] = 1.0f - snap.genome.uncertainty;   // Genome Coherence

    env->SetFloatArrayRegion(outArr, 0, 16, t);
    return outArr;
}

} // extern "C"
