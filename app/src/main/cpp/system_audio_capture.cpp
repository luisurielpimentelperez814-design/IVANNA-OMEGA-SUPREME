/*
 * IVANNA-OMEGA-SUPREME v1.5 — system_audio_capture.cpp
 * Implementación JNI para SystemAudioCapture.kt
 */

#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <cmath>

#define LOG_TAG "SystemAudioCapture"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Buffer circular para audio capturado
static constexpr int BUFFER_SIZE = 65536;
static float g_audio_buffer[BUFFER_SIZE];
static std::atomic<int> g_write_pos{0};
static std::atomic<int> g_read_pos{0};
static std::mutex g_buffer_mutex;

// Estadísticas
static std::atomic<float> g_last_rms_db{0.0f};
static std::atomic<float> g_last_peak_db{-120.0f};

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_SystemAudioCapture_nativeFeedBuffer(JNIEnv* env, jobject /*thiz*/, 
                                                                 jfloatArray audio_data, jint length) {
    if (!audio_data || length <= 0) return;
    
    jfloat* data = env->GetFloatArrayElements(audio_data, nullptr);
    if (!data) return;
    
    std::lock_guard<std::mutex> lock(g_buffer_mutex);
    
    // Calcular RMS y peak
    float sum_sq = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < length; ++i) {
        float sample = data[i];
        sum_sq += sample * sample;
        if (fabsf(sample) > peak) peak = fabsf(sample);
    }
    
    float rms = sqrtf(sum_sq / length);
    float rms_db = (rms > 0.0f) ? (20.0f * log10f(rms)) : -120.0f;
    float peak_db = (peak > 0.0f) ? (20.0f * log10f(peak)) : -120.0f;
    
    g_last_rms_db.store(rms_db, std::memory_order_relaxed);
    g_last_peak_db.store(peak_db, std::memory_order_relaxed);
    
    // Copiar al buffer circular
    int write_pos = g_write_pos.load(std::memory_order_relaxed);
    for (int i = 0; i < length; ++i) {
        g_audio_buffer[write_pos] = data[i];
        write_pos = (write_pos + 1) % BUFFER_SIZE;
    }
    g_write_pos.store(write_pos, std::memory_order_relaxed);
    
    env->ReleaseFloatArrayElements(audio_data, data, JNI_ABORT);
    
    LOGI("nativeFeedBuffer: %d samples, RMS=%.1f dB, Peak=%.1f dB", length, rms_db, peak_db);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_SystemAudioCapture_nativeClearBuffer(JNIEnv* env, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_buffer_mutex);
    g_write_pos.store(0, std::memory_order_relaxed);
    g_read_pos.store(0, std::memory_order_relaxed);
    memset(g_audio_buffer, 0, sizeof(g_audio_buffer));
    LOGI("nativeClearBuffer: buffer limpiado");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_audio_SystemAudioCapture_nativeHasData(JNIEnv* env, jobject /*thiz*/) {
    int write_pos = g_write_pos.load(std::memory_order_relaxed);
    int read_pos = g_read_pos.load(std::memory_order_relaxed);
    return (write_pos != read_pos) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastRmsDb(JNIEnv* env, jobject /*thiz*/) {
    return g_last_rms_db.load(std::memory_order_relaxed);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastPeakDb(JNIEnv* env, jobject /*thiz*/) {
    return g_last_peak_db.load(std::memory_order_relaxed);
}
