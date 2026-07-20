// ivanna_adaptive_jni.cpp
// ============================================================================
// JNI Bridge para Adaptive Engine
// Expone análisis y control automático a MainActivity
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================
//
// AUDITORÍA (colisión de símbolos JNI, build roto):
// El commit c089b34 implementó Java_com_ivanna_omega_core_IvannaNativeLib_
// nativeCreateAdaptiveEngine / nativeGetAdaptiveParameters /
// nativeGetAudioCharacteristics en jni/ivanna_omega_jni.cpp, redirigiendo
// el bloque "Modo MAGISTRAL" de MainActivity.kt al motor REAL en
// producción (g_adaptiveEngine / AdaptiveDecisionEngine, Fase 1-3) en vez
// de a este motor ("Motor B" / AdaptiveEngineCore, nunca alimentado con
// audio real — nativeAnalyzeAudio no tiene llamador en ningún Kotlin).
// Es la decisión correcta: reutiliza datos reales ya medidos en vez de
// duplicar un segundo motor con su propio estado, congelado en defaults.
//
// Pero este archivo YA tenía esas 3 mismas funciones (naming corregido en
// df68877, antes de c089b34) — mismo target de CMake, mismo símbolo C,
// misma firma JNI -> "multiple definition" en el linker. No compilaba.
//
// Regla de oro: no se borra AdaptiveEngineCore ni su lógica (sigue
// completa abajo, intacta, por si se retoma este motor como sensor
// independiente en el futuro — ver INTEGRATION_GUIDE.md). Solo se
// renombran los 3 exports JNI duplicados para que dejen de colisionar
// con la implementación real. nativeAnalyzeAudio y
// nativeDestroyAdaptiveEngine no colisionaban (sólo existen aquí) y
// quedan intactos.
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

// FIX (colisión de símbolos): renombrado desde
// Java_com_ivanna_omega_core_IvannaNativeLib_nativeCreateAdaptiveEngine —
// esa firma exacta ya la implementa jni/ivanna_omega_jni.cpp. Se conserva
// como función C++ normal (no exportada a Java) para no perder el código;
// nadie la llama todavía, gAdaptiveEngine queda null hasta que se decida
// revivir este motor como sensor independiente.
jlong ivanna_adaptive_jni_nativeCreateAdaptiveEngine_unused(JNIEnv *env, jclass clazz) {
    if (!gAdaptiveEngine) {
        gAdaptiveEngine = std::make_unique<AdaptiveEngineCore>();
        LOGI("Adaptive Engine creado");
    }
    return reinterpret_cast<jlong>(gAdaptiveEngine.get());
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeAnalyzeAudio(JNIEnv *env, jclass clazz, 
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

// FIX (colisión de símbolos): renombrado, ver nota arriba. Duplicaba
// Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAdaptiveParameters
// de jni/ivanna_omega_jni.cpp (esa versión sí devuelve datos reales).
jfloatArray ivanna_adaptive_jni_nativeGetAdaptiveParameters_unused(JNIEnv *env, jclass clazz) {
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

// FIX (colisión de símbolos): renombrado, ver nota arriba. Duplicaba
// Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAudioCharacteristics
// de jni/ivanna_omega_jni.cpp (esa versión sí devuelve datos reales).
jfloatArray ivanna_adaptive_jni_nativeGetAudioCharacteristics_unused(JNIEnv *env, jclass clazz) {
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
Java_com_ivanna_omega_core_IvannaNativeLib_nativeDestroyAdaptiveEngine(JNIEnv *env, jclass clazz) {
    if (gAdaptiveEngine) {
        gAdaptiveEngine.reset();
        LOGI("Adaptive Engine destruido");
    }
}

}  // extern "C"
