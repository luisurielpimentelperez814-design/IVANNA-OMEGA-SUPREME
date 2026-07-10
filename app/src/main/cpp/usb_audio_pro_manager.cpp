/*
 * IVANNA-OMEGA-SUPREME v1.5 — usb_audio_pro_manager.cpp
 * Implementación JNI para UsbAudioProManager.kt
 */

#include <jni.h>
#include <android/log.h>
#include <atomic>

#define LOG_TAG "UsbAudioProManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::atomic<bool> g_engine_running{false};

// FIX (mismatch de firma JNI — Kotlin declara `external fun
// nativeStartAsyncEngine(handle: Long, fd: Int)` sin valor de retorno, pero
// esta implementación tomaba (jint, jint) y devolvía jboolean. El binding
// dinámico de JNI resuelve por nombre, no por firma completa entre Kotlin y
// C++: al llamar se habría pasado un jlong de 64 bits donde el código nativo
// leía un jint de 32, desalineando ambos argumentos. Se corrige el tipo y
// cantidad de parámetros y el tipo de retorno para que coincidan exactamente
// con la declaración de Kotlin.
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeStartAsyncEngine(JNIEnv* /*env*/, jobject /*thiz*/,
                                                                       jlong handle, jint fd) {
    if (g_engine_running.load()) {
        LOGI("nativeStartAsyncEngine: engine ya está corriendo (handle=%lld, fd=%d)",
             static_cast<long long>(handle), fd);
        return;
    }

    // TODO: Implementar motor asíncrono USB Audio Pro real (ver spec aparte).
    // Por ahora, solo marcar como corriendo con los parámetros correctos.
    g_engine_running.store(true);
    LOGI("nativeStartAsyncEngine: engine iniciado (handle=%lld, fd=%d)",
         static_cast<long long>(handle), fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeStopAsyncEngine(JNIEnv* /*env*/, jobject /*thiz*/,
                                                                      jlong handle) {
    if (!g_engine_running.load()) {
        LOGI("nativeStopAsyncEngine: engine no está corriendo (handle=%lld)",
             static_cast<long long>(handle));
        return;
    }
    
    // TODO: Implementar detención limpia del motor
    g_engine_running.store(false);
    LOGI("nativeStopAsyncEngine: engine detenido (handle=%lld)", static_cast<long long>(handle));
}
