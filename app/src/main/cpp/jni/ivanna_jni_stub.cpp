#include <jni.h>
#include <android/log.h>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// ivanna_jni_stub.cpp — puente de compatibilidad legacy
//
// HISTORIAL:
//   FIXES_CONECTIVIDAD.md FIX 6 documenta que "AudioPipeline llamaba a
//   nativeSetAntiDolbyScoresJni pero no había implementación C++ para ese
//   símbolo". Este stub se creó para tapar ese hueco. Después el proyecto
//   consolidó la ruta canónica en:
//       AudioEngine.nativeSetAntiDolbyScoresStatic(voice, music, bass, silence)
//   implementada en audio_orchestrator.cpp (4 args, con soporte de `silence`).
//   La ruta legacy nativeSetAntiDolbyScoresJni (3 args, sin silence) quedó
//   sin caller Kotlin — grep -rn nativeSetAntiDolbyScoresJni --include=*.kt
//   devuelve cero.
//
// REGLA DE ORO — no borramos, mejoramos:
//   Este símbolo se preserva porque:
//     * Es parte de la ABI que un fork/rama podría estar linkeando.
//     * Ninja/CMake ya lo compila dentro de libivanna_omega.so y quitarlo
//       cambia el hash del .so sin razón funcional.
//   Se documenta explícitamente como legacy y su cuerpo se degrada a un
//   forward silencioso hacia la ruta canónica (ivanna_set_anti_dolby_scores)
//   cuando esté disponible, con un fallback logging simple.
// ─────────────────────────────────────────────────────────────────────────────

#define LOG_TAG "IVANNA-Stub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Símbolo canónico exportado por audio_orchestrator.cpp (FIXES_CONECTIVIDAD FIX 6).
// Se declara `weak` para que este .cpp compile en aislamiento (por ejemplo,
// en tests host-side que no linkean el orchestrator completo).
extern "C" __attribute__((weak))
void ivanna_set_anti_dolby_scores(float voice, float music, float bass, float silence);

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScoresJni(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jfloat speech, jfloat music, jfloat bass
) {
    if (!std::isfinite(speech) || !std::isfinite(music) || !std::isfinite(bass)) {
        LOGE("nativeSetAntiDolbyScoresJni: NaN/Inf ignorado");
        return;
    }
    const float sp = std::clamp(speech, 0.0f, 1.0f);
    const float mu = std::clamp(music,  0.0f, 1.0f);
    const float ba = std::clamp(bass,   0.0f, 1.0f);
    // "silence" no venía en la ABI vieja de 3 args — se deriva por
    // complemento aproximado del resto (mismo criterio usado en
    // AntiDolbyController cuando falta la señal explícita).
    const float si = std::clamp(1.0f - (sp + mu + ba), 0.0f, 1.0f);

    if (&ivanna_set_anti_dolby_scores != nullptr) {
        // Reenvía a la ruta canónica de 4 args del orquestador.
        ivanna_set_anti_dolby_scores(sp, mu, ba, si);
    } else {
        // Fallback si por alguna razón no se linkea el orchestrator
        // (host-side test build, module deshabilitado, etc.).
        LOGW("nativeSetAntiDolbyScoresJni (legacy sin orchestrator): speech=%.2f music=%.2f bass=%.2f silence=%.2f",
             sp, mu, ba, si);
    }
}

} // extern "C"
