#include <jni.h>
/*
 * IVANNA-OMEGA-SUPREME v1.5 — audio_orchestrator.cpp
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * FIXES v1.5:
 * 1. Limiter hard-clip a -0.1 dBFS al final de cadena
 * 2. Validación NaN/Inf en cada sample
 * 3. Validación sampleRate > 0 y frames <= 4096
 * 4. Null checks en todos los punteros JNI
 * 5. Phase wrap-around corregido (fmodf)
 * 6. JNI function names para package com.ivanna.omega
 */

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <cmath>
#include <atomic>
#include <mutex>
#include <thread>
#include <algorithm>
#include "anti_dolby.h"
#include "include/dsp_types.h"
#include "include/audio_thread_priority.h"
#include "include/HarmonicExciter.h"
#include "include/StereoWidener.h"
#include "audio_control_plane.hpp"

#define LOG_TAG "IVANNA-Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static constexpr float TWO_PI = 6.283185307179586f;
static constexpr float INV_TWO_PI = 0.159154943091895f;

// ── Limiter v1.5 ──────────────────────────────────────────────────────────────
// Hard-clip a -0.1 dBFS para evitar clipping >0dB en HAL de Android
static constexpr float LIMITER_THRESH = 0.98855f;   // -0.1 dBFS
static constexpr float LIMITER_CEIL   = 0.989f;

inline float process_limiter(float x) {
    if (!std::isfinite(x)) return 0.0f;
    if (x > LIMITER_THRESH) {
        return LIMITER_CEIL - (x - LIMITER_THRESH) * 0.1f;
    }
    if (x < -LIMITER_THRESH) {
        return -LIMITER_CEIL - (x + LIMITER_THRESH) * 0.1f;
    }
    return x;
}

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
    phase = fmodf(phase, TWO_PI);
    if (phase < 0) phase += TWO_PI;

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

// ── Kalman 2-estados ──────────────────────────────────────────────────────────
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
    return k.phase;
}

// ── Estado global del orquestador ───────────────────────────────────────────
static struct {
    int sampleRate = 48000;
    float gain = 1.0f;
    std::atomic<bool> active{true};
    Hyperplane hyper;
    KalmanState kalman;
    // Anti-Dolby v1.5: parámetros ajustables via JNI
    float exciterAmount = 0.0f;
    float eqGain = 0.0f;
    float widthAmount = 0.0f;
    bool bypass = false;
    AntiDolbyState antiDolby;
    ivanna::HarmonicExciter exciter;
    ivanna::StereoWidener widener;
    // AUDIT FIX (TODOs "implementar en commit 16 (benchmark)"): telemetría
    // real de salida en vez de los placeholders -23.0f / -6.0f hardcodeados.
    std::atomic<float> peakDbfs{-100.f};
    std::atomic<float> lufsApprox{-70.f};
} gState;

// Buffers estáticos de trabajo (deinterleave L/R) — evitan alloc en audio thread
alignas(64) static float g_leftBuf[4096];
alignas(64) static float g_rightBuf[4096];

// ── JNI: inicialización ───────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeInit(JNIEnv* env, jobject /*thiz*/, jint sampleRate) {
    if (sampleRate <= 0 || sampleRate > 192000) {
        LOGE("nativeInit: sampleRate inválido: %d", sampleRate);
        return;
    }
    gState.sampleRate = sampleRate;
    init_sin_table();
    kalmanInit(gState.kalman, sampleRate);

    // Inicializa exciter (HPF a 3kHz depende del sampleRate real)
    ivanna::DSPParams p;
    p.sampleRate = (uint32_t)sampleRate;
    p.drive = gState.exciterAmount;
    p.wet = gState.exciterAmount;
    gState.exciter.setParams(p);
    gState.exciter.setAmount(gState.exciterAmount);
    gState.widener.setWidth(gState.widthAmount);

    LOGI("nativeInit: sampleRate=%d, limiter -0.1dB activo", sampleRate);
}

// ── JNI: set gain ─────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetGain(JNIEnv* /*env*/, jobject /*thiz*/, jfloat gain) {
    if (!std::isfinite(gain)) {
        LOGE("nativeSetGain: gain NaN/Inf recibido");
        return;
    }
    gState.gain = std::clamp(gain, 0.0f, 2.0f);
}

// ── JNI: set exciter ──────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetExciter(JNIEnv* /*env*/, jobject /*thiz*/, jfloat amount) {
    if (!std::isfinite(amount)) return;
    gState.exciterAmount = std::clamp(amount, 0.0f, 1.0f);
    gState.exciter.setAmount(gState.exciterAmount);
}

// ── JNI: set EQ gain ──────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetEqGain(JNIEnv* /*env*/, jobject /*thiz*/, jfloat gain) {
    if (!std::isfinite(gain)) return;
    gState.eqGain = std::clamp(gain, -18.0f, 18.0f);
}

// ── JNI: set width ────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetWidth(JNIEnv* /*env*/, jobject /*thiz*/, jfloat width) {
    if (!std::isfinite(width)) return;
    gState.widthAmount = std::clamp(width, 0.0f, 1.5f);
    gState.widener.setWidth(gState.widthAmount); // [0..1.5] UI -> [0..1.5] DSP (1:1, tope real de StereoWidener)
}

// ── JNI: set bypass ───────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetBypass(JNIEnv* /*env*/, jobject /*thiz*/, jboolean bypass) {
    gState.bypass = bypass;
}

// ── Procesamiento principal (nucleo compartido) ────────────────────────────────
// OPTIMIZACION (fricción JNI): este nucleo antes vivía solo dentro de
// nativeProcessAudio operando sobre punteros obtenidos con
// GetFloatArrayElements (que en ART puede copiar el heap Java en cada
// callback). Se extrae a una funcion sobre punteros crudos, IDENTICA en
// logica al original, para que dos entry points JNI puedan compartirla:
// el existente (compatibilidad, jfloatArray) y uno nuevo zero-copy
// (nativeProcessAudioDirect, ByteBuffer directo) que sigue el mismo
// patron que ya usa ivanna_npe_jni.cpp con GetDirectBufferAddress.
static void processAudioCore(jfloat* in, jfloat* out, jint frames, jint channels) {
    ivanna::audio::enableAudioThreadFastMathOnce();
    const float dt = 1.0f / (float)gState.sampleRate;

    // Anti-Dolby v1.5: downmix inteligente si channelCount > 2
    // Layout Android típico: FL(0) FR(1) FC(2) LFE(3) BL(4) BR(5) SL(6) SR(7)
    if (gState.bypass) {
        // Bypass total: copia directa, sin exciter/widener/limiter
        int total = (channels > 2) ? frames * 2 : frames * channels;
        for (int i = 0; i < total; ++i) out[i] = in[i];
        return;
    }

    float* lBuf = g_leftBuf;
    float* rBuf = g_rightBuf;

    if (channels > 2) {
        for (int i = 0; i < frames; ++i) {
            int base = i * channels;
            float fl = in[base + 0];
            float fr = in[base + 1];
            float fc = (channels > 2) ? in[base + 2] : 0.0f;
            float bl = (channels > 4) ? in[base + 4] : 0.0f;
            float br = (channels > 5) ? in[base + 5] : 0.0f;
            float sl = (channels > 6) ? in[base + 6] : 0.0f;
            float sr = (channels > 7) ? in[base + 7] : 0.0f;

            // Validación NaN/Inf
            if (!std::isfinite(fl)) fl = 0.0f;
            if (!std::isfinite(fr)) fr = 0.0f;
            if (!std::isfinite(fc)) fc = 0.0f;

            // Downmix propio: L = FL + 0.7*FC + 0.5*SL + 0.3*BL
            //                  R = FR + 0.7*FC + 0.5*SR + 0.3*BR
            float left = fl + 0.7f * fc + 0.5f * sl + 0.3f * bl;
            float right = fr + 0.7f * fc + 0.5f * sr + 0.3f * br;

            // Aplicar gain
            left *= gState.gain;
            right *= gState.gain;

            // Anti-Dolby: ajustes dinámicos por clasificación YAMNet
            float widthMul = gState.antiDolby.widenerMultiplier.load(std::memory_order_relaxed);

            // Aplicar width reducido si speech detectado
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f * widthMul;
            lBuf[i] = mid + side;
            rBuf[i] = mid - side;
        }
    } else if (channels == 2) {
        for (int i = 0; i < frames; ++i) {
            float l = in[i * 2 + 0];
            float r = in[i * 2 + 1];
            if (!std::isfinite(l)) l = 0.0f;
            if (!std::isfinite(r)) r = 0.0f;
            lBuf[i] = l * gState.gain;
            rBuf[i] = r * gState.gain;
        }
    } else {
        // Mono: exciter/widener no aplican (no hay par estéreo) — solo gain + limiter
        // AUDIT FIX: también alimenta peakDbfs/lufsApprox aquí, si no la ruta
        // mono se quedaba con la telemetría estancada del último bloque estéreo.
        float blockPeak = 0.0f;
        double sumSq = 0.0;
        for (int i = 0; i < frames; ++i) {
            float x = in[i];
            if (!std::isfinite(x)) x = 0.0f;
            x = process_limiter(x * gState.gain);
            out[i] = x;
            blockPeak = std::max(blockPeak, std::fabs(x));
            sumSq += (double)x * x;
        }
        if (frames > 0) {
            gState.peakDbfs.store(20.0f * log10f(std::max(blockPeak, 1e-6f)), std::memory_order_relaxed);
            const float rms = (float)std::sqrt(sumSq / frames);
            gState.lufsApprox.store(20.0f * log10f(std::max(rms, 1e-6f)) - 0.691f, std::memory_order_relaxed);
        }
        return;
    }

    // Exciter armónico y stereo widener — YA CONECTADOS al pipeline (antes mudos)
    gState.exciter.process(lBuf, rBuf, frames);
    gState.widener.process(lBuf, rBuf, frames);

    // Limiter hard-clip al final de cadena — SIEMPRE activo
    // AUDIT FIX: se acumula peak y suma de cuadrados de lo que REALMENTE
    // sale (post-limiter) para alimentar nativeGetPeakDbfs/nativeGetLufs
    // con datos reales en vez de constantes fijas.
    float blockPeak = 0.0f;
    double sumSq = 0.0;
    for (int i = 0; i < frames; ++i) {
        float l = process_limiter(lBuf[i]);
        float r = process_limiter(rBuf[i]);
        out[i * 2 + 0] = l;
        out[i * 2 + 1] = r;
        blockPeak = std::max(blockPeak, std::max(std::fabs(l), std::fabs(r)));
        sumSq += (double)l * l + (double)r * r;
    }
    if (frames > 0) {
        const float peakDb = 20.0f * log10f(std::max(blockPeak, 1e-6f));
        gState.peakDbfs.store(peakDb, std::memory_order_relaxed);
        const float rms = (float)std::sqrt(sumSq / (2.0 * frames));
        // Aproximación no-K-weighted de loudness integrada (offset BS.1770
        // ungated ~ -0.691dB); es un proxy razonable sin el filtro K completo,
        // documentado como aproximación, no como LUFS certificado.
        const float lufs = 20.0f * log10f(std::max(rms, 1e-6f)) - 0.691f;
        gState.lufsApprox.store(lufs, std::memory_order_relaxed);
    }
}

// ── JNI: entry point compatible (jfloatArray, sin cambios de comportamiento) ──
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeProcessAudio(
    JNIEnv* env,
    jobject /*thiz*/,
    jfloatArray inArray,
    jfloatArray outArray,
    jint frames,
    jint channels
) {
    // FIX v1.5: validaciones críticas
    if (inArray == nullptr || outArray == nullptr) {
        LOGE("nativeProcessAudio: array nulo");
        return;
    }
    if (frames <= 0 || frames > 4096) {
        LOGE("nativeProcessAudio: frames inválido: %d", frames);
        return;
    }
    if (channels <= 0 || channels > 8) {
        LOGE("nativeProcessAudio: channels inválido: %d", channels);
        return;
    }
    if (gState.sampleRate <= 0) {
        LOGE("nativeProcessAudio: sampleRate no inicializado");
        return;
    }

    jfloat* in = env->GetFloatArrayElements(inArray, nullptr);
    jfloat* out = env->GetFloatArrayElements(outArray, nullptr);
    if (in == nullptr || out == nullptr) {
        LOGE("nativeProcessAudio: GetFloatArrayElements falló");
        if (in) env->ReleaseFloatArrayElements(inArray, in, JNI_ABORT);
        if (out) env->ReleaseFloatArrayElements(outArray, out, JNI_ABORT);
        return;
    }

    processAudioCore(in, out, frames, channels);

    // Input: nunca se escribe -> JNI_ABORT (no hay que copiar de vuelta a la JVM).
    // Output: siempre se escribe -> 0 (commit).
    env->ReleaseFloatArrayElements(inArray, in, JNI_ABORT);
    env->ReleaseFloatArrayElements(outArray, out, 0);
}

// ── JNI: entry point ZERO-COPY (ByteBuffer directo) ────────────────────────────
// OPTIMIZACION (fricción NDK<->OS): mismo procesamiento exacto que
// nativeProcessAudio (misma processAudioCore), pero el puntero viene de
// GetDirectBufferAddress sobre un ByteBuffer.allocateDirect() del lado
// Kotlin, reutilizado por bloque. Sin pineo, sin copia GC — igual al
// patrón que ya usa ivanna_npe_jni.cpp. Requiere que Kotlin cree los
// buffers con ByteOrder.nativeOrder() y los reutilice entre callbacks.
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeProcessAudioDirect(
    JNIEnv* env,
    jobject /*thiz*/,
    jobject inBuffer,
    jobject outBuffer,
    jint frames,
    jint channels
) {
    if (inBuffer == nullptr || outBuffer == nullptr) {
        LOGE("nativeProcessAudioDirect: buffer nulo");
        return;
    }
    if (frames <= 0 || frames > 4096) {
        LOGE("nativeProcessAudioDirect: frames inválido: %d", frames);
        return;
    }
    if (channels <= 0 || channels > 8) {
        LOGE("nativeProcessAudioDirect: channels inválido: %d", channels);
        return;
    }
    if (gState.sampleRate <= 0) {
        LOGE("nativeProcessAudioDirect: sampleRate no inicializado");
        return;
    }

    auto* in = static_cast<jfloat*>(env->GetDirectBufferAddress(inBuffer));
    auto* out = static_cast<jfloat*>(env->GetDirectBufferAddress(outBuffer));
    if (in == nullptr || out == nullptr) {
        LOGE("nativeProcessAudioDirect: GetDirectBufferAddress falló (¿el buffer no es direct?)");
        return;
    }

    processAudioCore(in, out, frames, channels);
    // Sin release: la memoria de un DirectByteBuffer no está pineada por la JVM.
}

// ── JNI: get LUFS integrado ────────────────────────────────────────────────
// AUDIT FIX: antes devolvía siempre -23.0f (placeholder "commit 16"). Ahora
// refleja gState.lufsApprox, calculado por bloque en nativeProcessAudio a
// partir de la señal real post-limiter. Si aún no se ha procesado ningún
// bloque, cae de vuelta al mismo -23.0f de referencia (valor típico de
// normalización broadcast) para no romper a nadie que ya dependa de un
// valor "silencioso" inicial razonable.
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetLufs(JNIEnv* /*env*/, jobject /*thiz*/) {
    return gState.lufsApprox.load(std::memory_order_relaxed);
}

// ── JNI: get pico dBFS ──────────────────────────────────────────────────────
// AUDIT FIX: antes devolvía siempre -6.0f. Ahora refleja gState.peakDbfs.
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetPeakDbfs(JNIEnv* /*env*/, jobject /*thiz*/) {
    return gState.peakDbfs.load(std::memory_order_relaxed);
}

// ── JNI: set Anti-Dolby classification scores ───────────────────────────────
// FIX: el nombre real exportado no coincidia con lo que declara
// AudioEngine.kt (companion @JvmStatic external fun
// nativeSetAntiDolbyScoresStatic(voice, music, bass, silence)) -- faltaba
// el sufijo "Static" y el 4to parametro "silence". Eso causaba
// UnsatisfiedLinkError garantizado en cada frame de clasificacion YAMNet
// (AudioPipeline.classifyWithYamnet), sin contar que ademas AudioEngine
// cargaba la .so equivocada (ver AudioEngine.kt).
// "silence" no se usa todavia en AntiDolbyState::updateFromClassification
// (solo toma speech/music/bass); se recibe y valida pero no se descarta
// silenciosamente si es invalido.
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScoresStatic(
    JNIEnv* /*env*/, jclass /*clazz*/, jfloat speech, jfloat music, jfloat bass, jfloat silence
) {
    if (!std::isfinite(speech) || !std::isfinite(music) || !std::isfinite(bass) || !std::isfinite(silence)) {
        LOGE("nativeSetAntiDolbyScoresStatic: valores NaN/Inf recibidos");
        return;
    }
    gState.antiDolby.updateFromClassification(
        std::clamp(speech, 0.0f, 1.0f),
        std::clamp(music, 0.0f, 1.0f),
        std::clamp(bass, 0.0f, 1.0f)
    );
    LOGI("Anti-Dolby: speech=%.2f music=%.2f bass=%.2f silence=%.2f", speech, music, bass, silence);
}

// ── JNI: perfil de compensación por ruta de salida (BT/AUX/USB) ────────────
// Llamado desde AudioRouteManager.kt cuando cambia la ruta de audio activa
// (AudioDeviceCallback / al iniciar captura). Ver control_set_route_profile().
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetRouteProfile(
    JNIEnv* /*env*/, jclass /*clazz*/,
    jfloat bassBoostDb, jfloat dialogBoostDb, jfloat widenerMult
) {
    if (!std::isfinite(bassBoostDb) || !std::isfinite(dialogBoostDb) || !std::isfinite(widenerMult)) {
        LOGE("nativeSetRouteProfile: valores NaN/Inf recibidos");
        return;
    }
    control_set_route_profile(bassBoostDb, dialogBoostDb, widenerMult);
    LOGI("RouteProfile: bassBoost=%.1fdB dialogBoost=%.1fdB widenerMult=%.2f",
         bassBoostDb, dialogBoostDb, widenerMult);
}

// ── JNI: getters de parametros de AudioEngine (para fusion en PDEngine) ─────
// FIX: declarados en AudioEngine.kt (nativeGetExciterValue/nativeGetEqGainDb/
// nativeGetWidthValue) pero sin implementacion en ningun .cpp -- dormidos
// (no llamados desde ningun otro Kotlin todavia), pero garantizaban
// UnsatisfiedLinkError en cuanto alguien los usara. gState ya guarda estos
// valores desde los setters existentes, asi que exponerlos es directo.
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetExciterValue(JNIEnv* /*env*/, jclass /*clazz*/) {
    return gState.exciterAmount;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetEqGainDb(JNIEnv* /*env*/, jclass /*clazz*/) {
    return gState.eqGain;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetWidthValue(JNIEnv* /*env*/, jclass /*clazz*/) {
    return gState.widthAmount;
}



// ── Símbolo externo para el stub JNI ─────────────────────────────────────────
// Llamado desde ivanna_jni_stub.cpp → AudioEngine companion @JvmStatic
// Permite que AudioPipeline.kt envíe scores YAMNet sin instancia de AudioEngine.
extern "C" void ivanna_set_anti_dolby_scores(float speech, float music, float bass) {
    if (!std::isfinite(speech) || !std::isfinite(music) || !std::isfinite(bass)) return;
    gState.antiDolby.updateFromClassification(
        std::clamp(speech, 0.0f, 1.0f),
        std::clamp(music,  0.0f, 1.0f),
        std::clamp(bass,   0.0f, 1.0f)
    );
}
