/*
 * IVANNA-FUSION TRASCENDENTAL
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * ARQUITECTURA:
 *   - AAudio OUTPUT: síntesis aditiva guiada por Kalman × Evolutiva × fusion_level
 *   - AudioRecord: captura micrófono → actualiza Kalman → exporta freq/phase al callback
 *   - fusion_level 0 = silencio; 0→1 = intensidad creciente del tono fusionado
 *   - El genoma evolutivo controla los pesos de los parciales (timbre)
 */

#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>

#define LOG_TAG "IVANNA-Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Hyperplane layout ─────────────────────────────────────────────────────────
struct Hyperplane {
    int32_t  biquad_coefs[64][5];
    float    kalman_state[3];
    uint8_t  poblacion_evolutiva[128][256];
    int16_t  temp_soc[10];
    uint8_t  sched_table[8][8][4][4][3];
    uint64_t seq_counter;
    uint8_t  active_buffer;
};

// ── Kalman cúbico ─────────────────────────────────────────────────────────────
struct KalmanState {
    float phase = 0.0f;
    float freq  = 0.0f;  // rad/sample
    float chirp = 0.0f;
    float P[3][3];
    float R = 1e-4f;
    bool  initialized = false;
};

static void kalmanInit(KalmanState &k, int sampleRate) {
    k.phase = 0.0f;
    k.freq  = 440.0f * 2.0f * (float)M_PI / (float)sampleRate;  // A4 inicial
    k.chirp = 0.0f;
    memset(k.P, 0, sizeof(k.P));
    k.P[0][0] = 0.1f; k.P[1][1] = 1.0f; k.P[2][2] = 0.001f;
    k.R = 1e-4f;
    k.initialized = true;
}

static float kalmanStep(KalmanState &k, float measurement, float dt) {
    const float qPhase = 1e-8f, qFreq = 1e-6f, qChirp = 1e-10f;
    float new_phase = k.phase + k.freq * dt + 0.5f * k.chirp * dt * dt;
    float new_freq  = k.freq  + k.chirp * dt;
    float new_chirp = k.chirp;
    k.P[0][0] += dt * dt * k.P[1][1] + qPhase;
    k.P[1][1] += qFreq;
    k.P[2][2] += qChirp;
    float S  = k.P[0][0] + k.R;
    float K0 = k.P[0][0] / S, K1 = k.P[1][0] / S, K2 = k.P[2][0] / S;
    float innov = measurement - new_phase;
    k.phase = new_phase + K0 * innov;
    k.freq  = new_freq  + K1 * innov;
    k.chirp = new_chirp + K2 * innov;
    k.P[0][0] *= (1.0f - K0);
    k.P[1][1] *= (1.0f - K1);
    k.P[2][2] *= (1.0f - K2);
    return innov;
}

// ── Engine ────────────────────────────────────────────────────────────────────
struct AudioEngine {
    AAudioStream *stream     = nullptr;
    Hyperplane   *hyperplane = nullptr;
    float  fusion_level      = 0.5f;
    int    sampleRate        = 48000;
    int    bitDepth          = 32;
    int64_t frameCounter     = 0;
    KalmanState kalman;
    std::atomic<float> phase_error_rms{0.0f};
    // Fase acumulada del callback (solo tocada por el hilo AAudio)
    float callbackPhase = 0.0f;
};

static AudioEngine g_engine;
static float g_phaseErrorAcc   = 0.0f;
static int   g_phaseErrorCount = 0;

// ── Estado Kalman exportado al callback (lock-free) ───────────────────────────
// Escrito por AudioRecord thread, leído por AAudio callback thread
static std::atomic<float> g_kFreq {0.0f};   // rad/sample
static std::atomic<float> g_kChirp{0.0f};   // rad/sample²

// ── Callback AAudio OUTPUT — síntesis aditiva guiada por Kalman + Evolutivo ──
aaudio_data_callback_result_t audioCallback(
    AAudioStream * /*stream*/,
    void         * /*userData*/,
    void          *audioData,
    int32_t        numFrames
) {
    const float fusion = g_engine.fusion_level;

    if (fusion < 1e-3f) {
        // Nivel de fusión 0 → silencio puro
        memset(audioData, 0, (size_t)numFrames * 2 * sizeof(float));
    } else {
        float *out = static_cast<float*>(audioData);

        // Frecuencia y chirp del Kalman (actualizados por AudioRecord thread)
        float freq_rs = g_kFreq.load(std::memory_order_relaxed);   // rad/sample
        const float chirp = g_kChirp.load(std::memory_order_relaxed);

        // Fallback: si Kalman aún no ha convergido, usar 440 Hz
        if (freq_rs < 1e-6f) {
            freq_rs = 440.0f * 2.0f * (float)M_PI / (float)g_engine.sampleRate;
        }

        // Pesos armónicos del genoma evolutivo (individuo 0, bytes 0-7 → parciales 1-8)
        float h[8] = {0.50f, 0.25f, 0.12f, 0.06f, 0.03f, 0.02f, 0.01f, 0.01f};
        if (g_engine.hyperplane) {
            const uint8_t *genome = g_engine.hyperplane->poblacion_evolutiva[0];
            float sum = 0.0f;
            for (int k = 0; k < 8; k++) {
                h[k] = static_cast<float>(genome[k]) / 255.0f + 1e-4f;
                sum += h[k];
            }
            if (sum > 1e-6f) { for (int k = 0; k < 8; k++) h[k] /= sum; }
        }

        // Amplitud máxima: fusion controla de 0 a 0.18 (nivel cómodo de escucha)
        const float amp = 0.18f * fusion;
        const float TWO_PI = 6.28318530718f;

        for (int i = 0; i < numFrames; i++) {
            // Fase instantánea con chirp (fm = base_freq + chirp_rate × t)
            const float ph = g_engine.callbackPhase
                           + freq_rs * (float)i
                           + 0.5f * chirp * (float)(i * i);

            // Síntesis aditiva: Σ h[k] · sin((k+1)·φ)
            float sample = 0.0f;
            for (int k = 0; k < 8; k++) {
                sample += h[k] * sinf(ph * (float)(k + 1));
            }
            sample *= amp;
            // Clip suave ±0.95
            if (sample >  0.95f) sample =  0.95f;
            if (sample < -0.95f) sample = -0.95f;

            out[i * 2]     = sample;   // L
            out[i * 2 + 1] = sample;   // R (mono → stereo)
        }

        // Avanzar fase y mantener en [−2π, 2π] para evitar pérdida de precisión
        g_engine.callbackPhase += freq_rs * (float)numFrames
                                 + 0.5f * chirp * (float)(numFrames * numFrames);
        while (g_engine.callbackPhase >  TWO_PI) g_engine.callbackPhase -= TWO_PI;
        while (g_engine.callbackPhase < -TWO_PI) g_engine.callbackPhase += TWO_PI;
    }

    g_engine.frameCounter += numFrames;

    if (g_engine.hyperplane) {
        g_engine.hyperplane->seq_counter++;
        g_engine.hyperplane->active_buffer = 0;
        g_engine.hyperplane->kalman_state[0] = g_engine.callbackPhase;
        g_engine.hyperplane->kalman_state[1] =
            g_kFreq.load(std::memory_order_relaxed) * (float)g_engine.sampleRate / (2.0f * (float)M_PI);
        g_engine.hyperplane->kalman_state[2] = g_kChirp.load(std::memory_order_relaxed);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// ── JNI ───────────────────────────────────────────────────────────────────────
extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ivannafusion_AudioEngine_nativeCreateEngine(
        JNIEnv * /*env*/, jobject /*thiz*/, jint sampleRate, jint bitDepth) {
    g_engine.sampleRate        = sampleRate;
    g_engine.bitDepth          = bitDepth;
    g_engine.callbackPhase     = 0.0f;
    g_engine.frameCounter      = 0;
    g_phaseErrorAcc            = 0.0f;
    g_phaseErrorCount          = 0;
    g_engine.phase_error_rms.store(0.0f);
    kalmanInit(g_engine.kalman, sampleRate);

    // Pre-cargar freq inicial para que el callback tenga un valor válido
    g_kFreq.store(440.0f * 2.0f * (float)M_PI / (float)sampleRate, std::memory_order_relaxed);
    g_kChirp.store(0.0f, std::memory_order_relaxed);

    AAudioStreamBuilder *builder;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setDataCallback(builder, audioCallback, nullptr);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &g_engine.stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        LOGE("Failed to open AAudio stream: %s", AAudio_convertResultToText(result));
        return 0L;
    }

    // Leer sample rate real que AAudio aceptó
    int32_t actualRate = AAudioStream_getSampleRate(g_engine.stream);
    g_engine.sampleRate = actualRate;
    // Recalcular freq inicial con la tasa real
    g_kFreq.store(440.0f * 2.0f * (float)M_PI / (float)actualRate, std::memory_order_relaxed);

    LOGI("AudioEngine OK: solicitado=%d Hz, efectivo=%d Hz, %d bits",
         sampleRate, actualRate, bitDepth);

    return reinterpret_cast<jlong>(&g_engine);
}

JNIEXPORT jint JNICALL
Java_com_ivannafusion_AudioEngine_nativeGetSampleRate(
        JNIEnv * /*env*/, jobject /*thiz*/) {
    return g_engine.sampleRate;
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeStartProcessing(
        JNIEnv * /*env*/, jobject /*thiz*/, jlong /*handle*/) {
    if (!g_engine.stream) return;
    aaudio_result_t r = AAudioStream_requestStart(g_engine.stream);
    if (r != AAUDIO_OK) LOGE("requestStart: %s", AAudio_convertResultToText(r));
    else LOGI("Audio processing started (síntesis aditiva Kalman+Evo)");
}

JNIEXPORT jint JNICALL
Java_com_ivannafusion_AudioEngine_nativeGetLatency(
        JNIEnv * /*env*/, jobject /*thiz*/, jlong /*handle*/) {
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

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeSetFusionLevel(
        JNIEnv * /*env*/, jobject /*thiz*/, jlong /*handle*/, jfloat level) {
    g_engine.fusion_level = level;
    if (g_engine.hyperplane) {
        float *f = reinterpret_cast<float*>(g_engine.hyperplane->poblacion_evolutiva[0]);
        f[0] = level;
    }
    LOGI("fusion_level → %.3f", level);
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_AudioEngine_nativeGetPhaseError(
        JNIEnv * /*env*/, jobject /*thiz*/, jlong /*handle*/) {
    return g_engine.phase_error_rms.load();
}

// Llamado desde Kotlin con muestras reales de AudioRecord (mono float)
JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeProcessCapture(
        JNIEnv *env, jobject /*thiz*/, jfloatArray samples, jint n) {
    jfloat *buf = env->GetFloatArrayElements(samples, nullptr);
    const float dt = 1.0f / (float)g_engine.sampleRate;
    float acc = 0.0f;
    int   cnt = 0;
    for (int i = 0; i < n; i++) {
        float innov = kalmanStep(g_engine.kalman, buf[i], dt);
        acc += innov * innov;
        cnt++;
    }
    env->ReleaseFloatArrayElements(samples, buf, JNI_ABORT);

    if (cnt > 0) {
        g_engine.phase_error_rms.store(std::sqrt(acc / cnt));
    }

    // *** Exportar estado Kalman al callback AAudio (lock-free) ***
    g_kFreq.store (g_engine.kalman.freq,  std::memory_order_relaxed);
    g_kChirp.store(g_engine.kalman.chirp, std::memory_order_relaxed);

    // Actualizar SHM
    if (g_engine.hyperplane) {
        g_engine.hyperplane->kalman_state[0] = g_engine.kalman.phase;
        g_engine.hyperplane->kalman_state[1] =
            g_engine.kalman.freq * (float)g_engine.sampleRate / (2.0f * (float)M_PI);
        g_engine.hyperplane->kalman_state[2] = g_engine.kalman.chirp;
    }
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeDestroyEngine(
        JNIEnv * /*env*/, jobject /*thiz*/, jlong /*handle*/) {
    if (g_engine.stream) {
        AAudioStream_requestStop(g_engine.stream);
        AAudioStream_close(g_engine.stream);
        g_engine.stream = nullptr;
    }
    LOGI("Audio engine destroyed");
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeSetHyperplane(
        JNIEnv * /*env*/, jobject /*thiz*/, jlong address) {
    g_engine.hyperplane = reinterpret_cast<Hyperplane*>((uintptr_t)address);
    LOGI("Hyperplane connected at 0x%llx", (long long)address);
}

// ── Bindings para IvannaNativeLib.kt ───────────────────────────────────────
// IvannaNativeLib.kt pide un ciclo de vida simplificado de 3 pasos
// (init/process/release) sin AAudio propio — pensado para procesar
// buffers ya capturados/entregados por Kotlin, en vez de manejar el
// stream de salida AAudio directamente (eso ya lo hace AudioEngine vía
// nativeCreateEngine/nativeStartProcessing). Se reutiliza el mismo
// filtro de Kalman (kalmanStep, g_engine.kalman) para no duplicar estado.

JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeInitAudioEngine(
        JNIEnv * /*env*/, jobject /*thiz*/, jint sampleRate, jint /*bufferSize*/) {
    g_engine.sampleRate = sampleRate;
    kalmanInit(g_engine.kalman, sampleRate);
    g_engine.phase_error_rms.store(0.0f);
    LOGI("IvannaNativeLib.nativeInitAudioEngine: sampleRate=%d", sampleRate);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeProcessAudio(
        JNIEnv *env, jobject /*thiz*/, jfloatArray inputBuffer, jfloatArray outputBuffer) {
    jsize n = env->GetArrayLength(inputBuffer);
    jsize nOut = env->GetArrayLength(outputBuffer);
    if (nOut < n) n = nOut;  // nunca escribir más allá del buffer de salida

    jfloat *inBuf  = env->GetFloatArrayElements(inputBuffer,  nullptr);
    jfloat *outBuf = env->GetFloatArrayElements(outputBuffer, nullptr);

    const float dt = (g_engine.sampleRate > 0) ? 1.0f / (float)g_engine.sampleRate
                                                : 1.0f / 48000.0f;
    for (int i = 0; i < n; i++) {
        float innov = kalmanStep(g_engine.kalman, inBuf[i], dt);
        outBuf[i] = inBuf[i] - innov * 0.0f + 0.0f; // passthrough explícito;
        // el suavizado/predicción real vive en nativePredictSamples
        // (phase_oracle.cpp) y en el motor DSP de ivanna_native_lib_v2.cpp
        // (EQ/Compresor/Exciter), que procesan el mismo audio capturado
        // a través de processBlock(). Este binding mantiene actualizado
        // el estado de fase (kalmanStep) en cada bloque entregado.
        outBuf[i] = inBuf[i];
    }

    env->ReleaseFloatArrayElements(inputBuffer,  inBuf,  JNI_ABORT);
    env->ReleaseFloatArrayElements(outputBuffer, outBuf, 0);
    return (jint)n;
}

JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeReleaseAudioEngine(
        JNIEnv * /*env*/, jobject /*thiz*/) {
    if (g_engine.stream) {
        AAudioStream_requestStop(g_engine.stream);
        AAudioStream_close(g_engine.stream);
        g_engine.stream = nullptr;
    }
    LOGI("IvannaNativeLib.nativeReleaseAudioEngine: motor liberado");
    return JNI_TRUE;
}

} // extern "C"
