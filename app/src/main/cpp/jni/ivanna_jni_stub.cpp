/*
 * ivanna_jni_stub.cpp — Stub JNI para AudioEngine.kt
 * © 2025-2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * FIX DE CONECTIVIDAD:
 *   Añadida Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScoresJni
 *   que faltaba. AudioEngine.kt declara este método @JvmStatic en companion
 *   pero el stub no lo implementaba → UnsatisfiedLinkError en runtime.
 *
 *   El stub delega al gState.antiDolby del audio_orchestrator.cpp via
 *   el símbolo externo ivanna_set_anti_dolby_scores().
 */

#include <jni.h>
#include <android/log.h>
#include <cmath>
#include <algorithm>

#define LOG_TAG "IVANNA-Stub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Símbolos externos definidos en audio_orchestrator.cpp
#ifdef __cplusplus
extern "C" {
#endif
    void ivanna_set_anti_dolby_scores(float speech, float music, float bass);
    void ivanna_set_route_profile(float bassBoostDb, float dialogBoostDb, float widenerMult);
    void ivanna_set_manifold_enabled(bool enabled);
    // AUDIT FIX: setters/getters añadidos al orchestrator para los sliders
    // de AudioEngine.kt que no tenían símbolo JNI (silenciosamente inoperantes).
    void  ivanna_set_master_gain(float db);
    void  ivanna_set_eq_gain(float db);
    void  ivanna_set_stereo_width(float width);
    float ivanna_get_lufs();
    float ivanna_get_peak_dbfs();
#ifdef __cplusplus
}
#endif

extern "C" {

// ── Stub para nativeSetAntiDolbyScoresJni (companion @JvmStatic) ─────────────
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScoresJni(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jfloat speech, jfloat music, jfloat bass
) {
    if (!std::isfinite(speech) || !std::isfinite(music) || !std::isfinite(bass)) {
        LOGE("nativeSetAntiDolbyScoresJni: valores NaN/Inf — ignorado");
        return;
    }
    ivanna_set_anti_dolby_scores(
        std::clamp(speech, 0.0f, 1.0f),
        std::clamp(music,  0.0f, 1.0f),
        std::clamp(bass,   0.0f, 1.0f)
    );
    LOGI("AntiDolby via stub: speech=%.2f music=%.2f bass=%.2f", speech, music, bass);
}

// ── Stub para nativeSetRouteProfileJni (companion @JvmStatic) ───────────────

// ── nativeSetRouteProfile (instancia AudioEngine, no @JvmStatic) ─────────────
// La versión @JvmStatic ya existe (nativeSetRouteProfileJni → companion object).
// Este símbolo cubre el external fun de INSTANCIA declarado en AudioEngine.kt:
//   private external fun nativeSetRouteProfile(...)
// Misma semántica: delega a ivanna_set_route_profile() → control plane.
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetRouteProfile(
    JNIEnv* /*env*/, jobject /*thiz*/,
    jfloat bassBoostDb, jfloat dialogBoostDb, jfloat widenerMult
) {
    if (!std::isfinite(bassBoostDb) || !std::isfinite(dialogBoostDb) || !std::isfinite(widenerMult)) {
        LOGE("nativeSetRouteProfile (instance): valores NaN/Inf — ignorado");
        return;
    }
    ivanna_set_route_profile(bassBoostDb, dialogBoostDb, widenerMult);
    LOGI("RouteProfile (instance): bass=%.2f dialog=%.2f widener=%.2f",
         bassBoostDb, dialogBoostDb, widenerMult);
}

// ── nativeSetManifoldEnabled (instancia AudioEngine) ─────────────────────────
// AUDIT FIX (JNI symbol missing): AudioEngine.kt declara este external fun
// sin símbolo JNI correspondiente → UnsatisfiedLinkError al invocarse desde
// el hilo de Kotlin. Se implementa el puente delegando en el símbolo
// externo ivanna_set_manifold_enabled() ya definido en audio_orchestrator.cpp
// (gState.manifoldEnabled). No cambia la firma pública ni la semántica.
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetManifoldEnabled(
    JNIEnv* /*env*/, jobject /*thiz*/,
    jboolean enabled
) {
    const bool en = (enabled == JNI_TRUE);
    ivanna_set_manifold_enabled(en);
    LOGI("ManifoldEnabled (instance): %s", en ? "true" : "false");
}

// ── AUDIT FIX: puentes para los 6 external fun huérfanos de AudioEngine.kt ───
// AudioEngine.kt declara nativeSetGain / nativeSetEqGain / nativeSetWidth /
// nativeGetLufs / nativeGetPeakDbfs y las invoca en runtime (setGain, setEqGain,
// setWidth) envueltas en runCatching — sin símbolo C++ el UnsatisfiedLinkError
// se tragaba en silencio y los sliders no hacían nada. Se implementan aquí
// delegando al OrchestratorState (audio_orchestrator.cpp), que ahora tiene
// campos masterGainDb / eqGainDb / stereoWidth aplicados en ivanna_orchestrate.
// nativeProcessAudio se omite: AudioEngine.kt nunca la invoca (pipeline
// confirmado huérfano, ver comentario en VoiceProtectionController.kt:23).

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetGain(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat gain
) {
    if (!std::isfinite(gain)) { LOGE("nativeSetGain: NaN/Inf — ignorado"); return; }
    ivanna_set_master_gain(gain);
    LOGI("MasterGain: %.2f dB", gain);
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetEqGain(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat gain
) {
    if (!std::isfinite(gain)) { LOGE("nativeSetEqGain: NaN/Inf — ignorado"); return; }
    ivanna_set_eq_gain(gain);
    LOGI("EqGain: %.2f dB", gain);
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetWidth(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat width
) {
    if (!std::isfinite(width)) { LOGE("nativeSetWidth: NaN/Inf — ignorado"); return; }
    ivanna_set_stereo_width(width);
    LOGI("StereoWidth: %.2f", width);
}

JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeGetLufs(
    JNIEnv* /*env*/, jobject /*thiz*/
) {
    return ivanna_get_lufs();
}

JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeGetPeakDbfs(
    JNIEnv* /*env*/, jobject /*thiz*/
) {
    return ivanna_get_peak_dbfs();
}

}


// ── AudioEngine.kt missing JNI bindings ──

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeInit(
    JNIEnv* /*env*/, jobject /*thiz*/, jint sampleRate) {
    LOGI("AudioEngine_nativeInit: sampleRate=%d", sampleRate);
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetExciter(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat amount) {
    if (!std::isfinite(amount)) { LOGE("nativeSetExciter: NaN/Inf — ignorado"); return; }
    LOGI("AudioEngine_nativeSetExciter: amount=%.2f", amount);
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetBypass(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean bypass) {
    LOGI("AudioEngine_nativeSetBypass: %s", bypass ? "true" : "false");
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeSetRouteProfileJni(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jfloat bassBoostDb, jfloat dialogBoostDb, jfloat widenerMult) {
    if (!std::isfinite(bassBoostDb) || !std::isfinite(dialogBoostDb) || !std::isfinite(widenerMult)) {
        LOGE("nativeSetRouteProfileJni (companion): valores NaN/Inf — ignorado");
        return;
    }
    ivanna_set_route_profile(bassBoostDb, dialogBoostDb, widenerMult);
    LOGI("RouteProfile (companion): bass=%.2f dialog=%.2f widener=%.2f",
         bassBoostDb, dialogBoostDb, widenerMult);
}

