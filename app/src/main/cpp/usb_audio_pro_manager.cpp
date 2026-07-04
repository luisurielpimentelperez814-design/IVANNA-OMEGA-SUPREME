/*
 * IVANNA-OMEGA-SUPREME v1.5 — usb_audio_pro_manager.cpp
 * Implementación JNI para UsbAudioProManager.kt
 *
 * AUDIT FIX (bug crítico, no solo TODO cosmético):
 * Las firmas nativas de aquí NO coincidían con lo que Kotlin declara:
 *
 *   Kotlin: external fun nativeStartAsyncEngine(handle: Long, fd: Int)      -> firma JNI (JI)V
 *   C++ (antes): (JNIEnv*, jobject, jint sample_rate, jint channel_count)  -> firma JNI (II)Z
 *
 *   Kotlin: external fun nativeStopAsyncEngine(handle: Long)                -> firma JNI (J)V
 *   C++ (antes): (JNIEnv*, jobject)                                        -> firma JNI ()V
 *
 * Ninguna de las dos coincidía en tipos ni cantidad de parámetros. En cuanto
 * UsbAudioProManager.startAsyncStreaming()/stopStreaming() se invocaran desde
 * Kotlin, el runtime habría lanzado UnsatisfiedLinkError al no encontrar el
 * símbolo nativo con esa firma exacta — un crash garantizado, no un bug
 * latente. Se corrigen las firmas para que coincidan con Kotlin.
 *
 * El motor asíncrono real (leer/escribir directamente sobre el fd del
 * endpoint isochronous) SIGUE sin implementarse — eso requiere el loop de
 * transferencia USB real y no se puede fabricar sin arriesgar romper el
 * dispositivo del usuario. Se deja explícito abajo qué falta, en vez de
 * aparentar que ya está completo.
 */

#include <jni.h>
#include <android/log.h>
#include <mutex>

#define LOG_TAG "UsbAudioProManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// AUDIT FIX #6: los 3 campos correlacionados (running/handle/fd) formaban
// una transición de estado que debía verse atómicamente como conjunto; con
// 3 std::atomic independientes, un hilo podía ver running=true con handle/fd
// aún no actualizados (o viceversa en el stop). Un mutex + struct hace la
// transición indivisible.
struct EngineState {
    bool  running = false;
    jlong handle  = 0;
    int   fd      = -1;
};
static std::mutex g_engine_mutex;
static EngineState g_engine_state;

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeStartAsyncEngine(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jint fd) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine_state.running) {
        LOGI("nativeStartAsyncEngine: engine ya está corriendo (handle=%lld)",
             (long long)g_engine_state.handle);
        return;
    }
    if (fd < 0) {
        LOGE("nativeStartAsyncEngine: fd inválido (%d), no se inicia", fd);
        return;
    }

    g_engine_state.handle = handle;
    g_engine_state.fd = fd;
    g_engine_state.running = true;

    // TODO (real, honesto): falta el loop de transferencia isochronous/bulk
    // sobre g_engine_state.fd (poll() del FD como "slave" del reloj del DAC,
    // según describe UsbAudioProManager.kt::startAsyncStreaming). Por ahora
    // solo se registra el estado; no hay streaming de audio real todavía.
    LOGI("nativeStartAsyncEngine: handle=%lld fd=%d — estado registrado, "
         "loop de streaming aún pendiente", (long long)handle, fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeStopAsyncEngine(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine_state.running) {
        LOGI("nativeStopAsyncEngine: engine no está corriendo");
        return;
    }
    if (handle != g_engine_state.handle) {
        LOGE("nativeStopAsyncEngine: handle=%lld no coincide con el activo=%lld",
             (long long)handle, (long long)g_engine_state.handle);
    }

    g_engine_state.running = false;
    g_engine_state.fd = -1;
    LOGI("nativeStopAsyncEngine: engine detenido (handle=%lld)", (long long)handle);
}
