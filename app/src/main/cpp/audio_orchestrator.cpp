/*
 * IVANNA-FUSION TRASCENDENTAL - OPTIMIZADO v2
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 */

#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#define LOG_TAG "IVANNA-Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static constexpr float TWO_PI = 6.283185307179586f;
static constexpr float INV_TWO_PI = 0.159154943091895f;

// ── Tabla seno con interpolación cúbica ───────────────────────────────────────
static constexpr int SIN_TABLE_SIZE = 4096;
alignas(64) static float sin_table[SIN_TABLE_SIZE + 4];
static std::once_flag sin_once;

static void init_sin_table() {
    std::call_once(sin_once, []{
        for (int i = 0; i < SIN_TABLE_SIZE + 4; i++) {
            float phase = TWO_PI * (i - 1) / (float)SIN_TABLE_SIZE;
            sin_table[i] = sinf(phase);
        }
    });
}

static inline float fast_sin(float phase) {
    phase -= floorf(phase * INV_TWO_PI) * TWO_PI;
    float idx = phase * (SIN_TABLE_SIZE * INV_TWO_PI);
    int i = (int)idx;
    float f = idx - i;
    i = (i + 1) & (SIN_TABLE_SIZE - 1);

    float y0 = sin_table[i];
    float y1 = sin_table[i+1];
    float y2 = sin_table[i+2];
    float y3 = sin_table[i+3];
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f*y1 + 2.0f*y2 - 0.5f*y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3*f + c2)*f + c1)*f + c0;
}

// ── Hyperplane reducido ───────────────────────────────────────────────────────
struct alignas(64) Hyperplane {
    float kalman_state[3];
    uint64_t seq_counter = 0;
    uint8_t active_buffer = 0;
};

// ── Kalman 2-estados correcto ─────────────────────────────────────────────────
struct alignas(32) KalmanState {
    float phase = 0.0f;
    float freq = 0.0f;
    float P00 = 0.1f, P01 = 0.0f, P11 = 1.0f;
    float R = 1e-4f;
};

static inline void kalmanInit(KalmanState &k, int sr) {
    k.phase = 0.0f;
    k.freq = 440.0f * TWO_PI / (float)sr;
    k.P00 = 0.1f; k.P01 = 0.0f; k.P11 = 1.0f;
}

static inline float kalmanStep(KalmanState &k, float meas, float dt) {
    const float qPhase = 1e-8f, qFreq = 1e-6f;
    float pred_phase = k.phase + k.freq * dt;
    float pred_freq = k.freq;
    k.P00 += dt*(k.P01 + k.P01 + dt*k.P11) + qPhase;
    k.P01 += dt*k.P11;
    k.P11 += qFreq;
    float y = meas - pred_phase;
    float S = k.P00 + k.R;
    float K0 = k.P00 / S;
    float K1 = k.P01 / S;
    k.phase = pred_phase + K0 * y;
    k.freq = pred_freq + K1 * y;
    k.P00 *= (1.0f - K0);
    k.P01 *= (1.0f - K0);
    k.P11 -= K1 * k.P01;
    return y;
}

// ── Engine ────────────────────────────────────────────────────────────────────
struct alignas(64) AudioEngine {
    AAudioStream *stream = nullptr;
    Hyperplane *hyperplane = nullptr;
    float fusion_level = 0.5f;
    int sampleRate = 48000;
    int64_t frameCounter = 0;
    KalmanState kalman;
    std::atomic<float> phase_error_rms{0.0f};
    float phase = 0.0f;
    float h[8] alignas(32) = {0.5f,0.25f,0.12f,0.06f,0.03f,0.02f,0.01f,0.01f};
    uint64_t genome_hash = 0;
};

static AudioEngine g_engine;
static std::atomic<float> g_kFreq{0.0f};
static std::atomic<float> g_kChirp{0.0f};

static uint64_t hash_genome(const uint8_t* p) {
    uint64_t h = 1469598103934665603ULL;
    for (int i=0;i<32;i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

aaudio_data_callback_result_t audioCallback(
    AAudioStream*, void*, void* audioData, int32_t numFrames) {

    init_sin_table();
    float* __restrict__ out = (float*)audioData;
    float fusion = g_engine.fusion_level;

    if (fusion < 1e-3f) {
        memset(out, 0, numFrames * 2 * sizeof(float));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    float freq = g_kFreq.load(std::memory_order_relaxed);
    if (freq < 1e-6f) freq = 440.0f * TWO_PI / g_engine.sampleRate;
    float chirp = g_kChirp.load(std::memory_order_relaxed);
    float phase = g_engine.phase;
    float amp = 0.18f * fusion;

    if (g_engine.hyperplane) {
        // aquí conectarías tu genoma real
    }

    for (int i=0; i<numFrames; i++) {
        float sample = 0.0f;
        float base = phase;
        #pragma clang loop vectorize(enable)
        for (int k=0;k<8;k++) sample += g_engine.h[k] * fast_sin(base * (k+1));
        sample *= amp;
        sample = sample > 0.95f? 0.95f : (sample < -0.95f? -0.95f : sample);
        out[2*i] = sample;
        out[2*i+1] = sample;
        phase += freq + chirp * i;
        if (phase >= TWO_PI) phase -= TWO_PI;
    }
    g_engine.phase = phase;
    g_engine.frameCounter += numFrames;

    if (g_engine.hyperplane) {
        g_engine.hyperplane->seq_counter++;
        g_engine.hyperplane->kalman_state[0] = phase;
        g_engine.hyperplane->kalman_state[1] = freq * g_engine.sampleRate * INV_TWO_PI;
        g_engine.hyperplane->kalman_state[2] = chirp;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

extern "C" {

JNIEXPORT jlong JNICALL Java_com_ivannafusion_AudioEngine_nativeCreateEngine(
    JNIEnv*, jobject, jint sr, jint) {
    init_sin_table();
    g_engine.sampleRate = sr;
    g_engine.phase = 0;
    kalmanInit(g_engine.kalman, sr);
    g_kFreq.store(440.0f * TWO_PI / sr);
    g_kChirp.store(0);

    AAudioStreamBuilder* b;
    AAudio_createStreamBuilder(&b);
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setSampleRate(b, sr);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setDataCallback(b, audioCallback, nullptr);

    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_engine.stream);
    AAudioStreamBuilder_delete(b);
    if (r!= AAUDIO_OK) { LOGE("AAudio open failed"); return 0; }

    g_engine.sampleRate = AAudioStream_getSampleRate(g_engine.stream);
    LOGI("Engine listo %d Hz", g_engine.sampleRate);
    return (jlong)&g_engine;
}

JNIEXPORT jint JNICALL Java_com_ivannafusion_AudioEngine_nativeGetSampleRate(
    JNIEnv*, jobject) {
    return g_engine.sampleRate;
}

JNIEXPORT void JNICALL Java_com_ivannafusion_AudioEngine_nativeStartProcessing(
    JNIEnv*, jobject, jlong) {
    if (!g_engine.stream) return;
    aaudio_result_t r = AAudioStream_requestStart(g_engine.stream);
    if (r != AAUDIO_OK) LOGE("requestStart: %s", AAudio_convertResultToText(r));
    else LOGI("Audio processing started");
}

JNIEXPORT jint JNICALL Java_com_ivannafusion_AudioEngine_nativeGetLatency(
    JNIEnv*, jobject, jlong) {
    if (!g_engine.stream) return 0;
    int64_t framePosition = 0, presentationNs = 0;
    aaudio_result_t r = AAudioStream_getTimestamp(
        g_engine.stream, CLOCK_MONOTONIC, &framePosition, &presentationNs);
    if (r != AAUDIO_OK) return 0;

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t nowNs = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    int64_t writtenFrames = AAudioStream_getFramesWritten(g_engine.stream);
    int64_t pendingFrames = writtenFrames - framePosition;
    if (pendingFrames < 0) pendingFrames = 0;
    int64_t latencyNs = presentationNs - nowNs +
                        pendingFrames * 1000000000LL / g_engine.sampleRate;
    if (latencyNs < 0) latencyNs = 0;
    return (jint)(latencyNs / 1000);
}

JNIEXPORT void JNICALL Java_com_ivannafusion_AudioEngine_nativeSetFusionLevel(
    JNIEnv*, jobject, jlong, jfloat level) {
    g_engine.fusion_level = level;
    LOGI("fusion_level → %.3f", level);
}

JNIEXPORT jfloat JNICALL Java_com_ivannafusion_AudioEngine_nativeGetPhaseError(
    JNIEnv*, jobject, jlong) {
    return g_engine.phase_error_rms.load();
}

JNIEXPORT void JNICALL Java_com_ivannafusion_AudioEngine_nativeProcessCapture(
    JNIEnv *env, jobject, jfloatArray samples, jint n) {
    jfloat *buf = env->GetFloatArrayElements(samples, nullptr);
    const float dt = 1.0f / (float)g_engine.sampleRate;
    float acc = 0.0f;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        float innov = kalmanStep(g_engine.kalman, buf[i], dt);
        acc += innov * innov;
        cnt++;
    }
    env->ReleaseFloatArrayElements(samples, buf, JNI_ABORT);

    if (cnt > 0) {
        g_engine.phase_error_rms.store(sqrtf(acc / cnt));
    }

    g_kFreq.store(g_engine.kalman.freq, std::memory_order_relaxed);
    g_kChirp.store(g_engine.kalman.phase, std::memory_order_relaxed);

    if (g_engine.hyperplane) {
        g_engine.hyperplane->kalman_state[0] = g_engine.kalman.phase;
        g_engine.hyperplane->kalman_state[1] =
            g_engine.kalman.freq * (float)g_engine.sampleRate * INV_TWO_PI;
    }
}

JNIEXPORT void JNICALL Java_com_ivannafusion_AudioEngine_nativeDestroyEngine(
    JNIEnv*, jobject, jlong) {
    if (g_engine.stream) {
        AAudioStream_requestStop(g_engine.stream);
        AAudioStream_close(g_engine.stream);
        g_engine.stream = nullptr;
    }
    LOGI("Audio engine destroyed");
}

JNIEXPORT void JNICALL Java_com_ivannafusion_AudioEngine_nativeSetHyperplane(
    JNIEnv*, jobject, jlong addr) {
    g_engine.hyperplane = (Hyperplane*)(uintptr_t)addr;
    g_engine.genome_hash = 0;
}

JNIEXPORT jboolean JNICALL Java_com_ivannafusion_IvannaNativeLib_nativeInitAudioEngine(
    JNIEnv*, jobject, jint sr, jint) {
    g_engine.sampleRate = sr;
    kalmanInit(g_engine.kalman, sr);
    g_engine.phase_error_rms.store(0.0f);
    LOGI("IvannaNativeLib.nativeInitAudioEngine: sampleRate=%d", sr);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL Java_com_ivannafusion_IvannaNativeLib_nativeProcessAudio(
    JNIEnv *env, jobject, jfloatArray inputBuffer, jfloatArray outputBuffer) {
    jsize n = env->GetArrayLength(inputBuffer);
    jsize nOut = env->GetArrayLength(outputBuffer);
    if (nOut < n) n = nOut;

    jfloat *inBuf = env->GetFloatArrayElements(inputBuffer, nullptr);
    jfloat *outBuf = env->GetFloatArrayElements(outputBuffer, nullptr);

    const float dt = (g_engine.sampleRate > 0) ? 1.0f / (float)g_engine.sampleRate : 1.0f / 48000.0f;
    for (int i = 0; i < n; i++) {
        kalmanStep(g_engine.kalman, inBuf[i], dt);
        outBuf[i] = inBuf[i];
    }

    env->ReleaseFloatArrayElements(inputBuffer, inBuf, JNI_ABORT);
    env->ReleaseFloatArrayElements(outputBuffer, outBuf, 0);
    return n;
}

} // extern C
