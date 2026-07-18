// ivanna_adaptive_jni.cpp
// ============================================================================
// JNI Bridge para Adaptive Engine
// Expone análisis y control automático a MainActivity
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

#include <jni.h>
#include "../adaptive_engine_core.hpp"
#include <android/log.h>
#include <memory>

#define LOG_TAG "IvannaAdaptiveJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace ivanna::adaptive;

static std::unique_ptr<AdaptiveEngineCore> gAdaptiveEngine;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeCreateAdaptiveEngine(JNIEnv *env, jclass clazz) {
    if (!gAdaptiveEngine) {
        gAdaptiveEngine = std::make_unique<AdaptiveEngineCore>();
        LOGI("Adaptive Engine creado");
    }
    return reinterpret_cast<jlong>(gAdaptiveEngine.get());
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeAnalyzeAudio(JNIEnv *env, jclass clazz, 
                                                         jfloatArray audioBuffer) {
    if (!gAdaptiveEngine) return;
    
    jfloat *buf = env->GetFloatArrayElements(audioBuffer, nullptr);
    int len = env->GetArrayLength(audioBuffer);
    
    if (buf && len > 0) {
        gAdaptiveEngine->analyzeAudio(buf, len);
        gAdaptiveEngine->computeAdaptiveParameters();
        LOGI("Análisis completado: RMS=%.3f, Percussiveness=%.2f", 
             gAdaptiveEngine->getCharacteristics().rms,
             gAdaptiveEngine->getCharacteristics().percussiveness);
    }
    
    env->ReleaseFloatArrayElements(audioBuffer, buf, JNI_ABORT);
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeGetAdaptiveParameters(JNIEnv *env, jclass clazz) {
    if (!gAdaptiveEngine) {
        jfloatArray result = env->NewFloatArray(12);
        return result;
    }
    
    const auto& params = gAdaptiveEngine->getSmoothParameters();
    
    jfloat paramsArray[12] = {
        params.compressorThreshold,
        params.compressorRatio,
        params.exciterAmount,
        params.stereoWidth,
        params.eqBass,
        params.eqMid,
        params.eqTreble,
        params.overallGain,
        params.compressorAttack,
        params.compressorRelease,
        params.spatialIntensity,
        params.safetyMargin
    };
    
    jfloatArray result = env->NewFloatArray(12);
    env->SetFloatArrayRegion(result, 0, 12, paramsArray);
    
    LOGI("Parámetros: Compressor[%.1f, %.1f], Exciter=%.2f, Stereo=%.2f, Gain=%.2f",
         params.compressorThreshold, params.compressorRatio, params.exciterAmount,
         params.stereoWidth, params.overallGain);
    
    return result;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeGetAudioCharacteristics(JNIEnv *env, jclass clazz) {
    if (!gAdaptiveEngine) {
        jfloatArray result = env->NewFloatArray(8);
        return result;
    }
    
    const auto& chars = gAdaptiveEngine->getCharacteristics();
    
    jfloat charsArray[8] = {
        chars.rms,
        chars.peak,
        chars.percussiveness,
        chars.tonality,
        chars.reverbAmount,
        chars.dynamicRange,
        chars.spectralCentroid,
        chars.spectralSpread
    };
    
    jfloatArray result = env->NewFloatArray(8);
    env->SetFloatArrayRegion(result, 0, 8, charsArray);
    
    LOGI("Características: RMS=%.3f, Percussiveness=%.2f, Tonality=%.2f, Reverb=%.2f",
         chars.rms, chars.percussiveness, chars.tonality, chars.reverbAmount);
    
    return result;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_IvannaNativeLib_nativeDestroyAdaptiveEngine(JNIEnv *env, jclass clazz) {
    if (gAdaptiveEngine) {
        gAdaptiveEngine.reset();
        LOGI("Adaptive Engine destruido");
    }
}

}  // extern "C"
