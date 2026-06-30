#include <jni.h>
#include <android/log.h>
#include <cmath>
#include <algorithm>

#define LOG_TAG "IVANNA-Stub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
    LOGI("AntiDolby scores: speech=%.2f music=%.2f bass=%.2f", 
         std::clamp(speech,0.0f,1.0f),
         std::clamp(music,0.0f,1.0f),
         std::clamp(bass,0.0f,1.0f));
}

} // extern "C"
