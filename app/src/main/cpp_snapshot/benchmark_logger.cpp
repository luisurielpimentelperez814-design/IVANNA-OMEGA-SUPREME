/*
 * benchmark_logger.cpp — Benchmark interno IVANNA vs Dolby
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * FIX v1.5: Guarda métricas en CSV para comparación objetiva.
 * Estados: 0=IVANNA, 1=DolbyOFF, 2=DolbyON
 */

#include <android/log.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <fstream>
#include <jni.h>

#define LOG_TAG "OmegaBenchmark"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static constexpr const char* BENCHMARK_PATH = "/data/local/tmp/ivanna_benchmark.csv";
static bool g_benchmark_initialized = false;

struct BenchmarkEntry {
    double timestamp;
    float lufs_integrated;
    float peak_dbfs;
    float yamnet_speech;
    float yamnet_music;
    float yamnet_bass;
    int dolby_state;  // 0=IVANNA, 1=DolbyOFF, 2=DolbyON
};

static void omega_benchmark_init() {
    if (g_benchmark_initialized) return;

    std::ofstream file(BENCHMARK_PATH, std::ios::out | std::ios::trunc);
    if (file.is_open()) {
        file << "timestamp,lufs_integrated,peak_dbfs,yamnet_speech,yamnet_music,yamnet_bass,dolby_state\n";
        file.close();
        g_benchmark_initialized = true;
        LOGI("Benchmark inicializado: %s", BENCHMARK_PATH);
    }
}

extern "C" void omega_benchmark_log(
    float lufs,
    float peak,
    float speech,
    float music,
    float bass,
    int dolbyState
) {
    if (!g_benchmark_initialized) omega_benchmark_init();

    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", tm_info);

    std::ofstream file(BENCHMARK_PATH, std::ios::out | std::ios::app);
    if (file.is_open()) {
        file << timeStr << ","
             << lufs << ","
             << peak << ","
             << speech << ","
             << music << ","
             << bass << ","
             << dolbyState << "\n";
        file.close();
    }

    LOGI("Benchmark: LUFS=%.2f peak=%.2f speech=%.2f music=%.2f bass=%.2f state=%d",
         lufs, peak, speech, music, bass, dolbyState);
}

// ── JNI export para Kotlin ────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeLogBenchmark(
    JNIEnv* /*env*/,
    jobject /*thiz*/,
    jfloat lufs,
    jfloat peak,
    jfloat speech,
    jfloat music,
    jfloat bass,
    jint dolbyState
) {
    omega_benchmark_log(lufs, peak, speech, music, bass, dolbyState);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetBenchmarkPath(
    JNIEnv* env,
    jobject /*thiz*/
) {
    return env->NewStringUTF(BENCHMARK_PATH);
}
