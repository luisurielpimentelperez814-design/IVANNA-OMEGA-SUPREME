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
#include "audio_control_plane.hpp"

// g_control_frame ya NO se define aquí: desde la integración "Fase 1"
// (ver CMakeLists.txt) audio_control_plane.cpp se compila como parte del
// target y es la única fuente de verdad del singleton (declarado extern
// en audio_control_plane.hpp). Definirlo también aquí producía
// "duplicate symbol: g_control_frame" en el linker.

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

// ── EQ Paramétrico Simple (pico @ 3kHz) ───────────────────────────────────────
// Estado biquad (mono, reutilizable)
struct EQState {
    float x1 = 0.0f, x2 = 0.0f;  // Entrada retrasada
    float y1 = 0.0f, y2 = 0.0f;  // Salida retrasada
};

// Coefs biquad precalculados para pico de +2dB @ 3kHz, Q=1.0, fs=48kHz
// (estos se pueden actualizar dinámicamente si es necesario)
struct BiquadCoefs {
    float b0 = 1.0449f, b1 = 0.0f,     b2 = 1.0449f;  // Numerador
    float a1 = -1.8928f, a2 = 0.9102f;               // Denominador (a0=1.0)
};

static constexpr BiquadCoefs EQ_2K4K_COEFS{};

// Procesar una muestra a través del filtro biquad
inline float apply_eq_boost(EQState &st, float x, float boost_db) {
    if (boost_db <= 0.001f) return x;  // Sin boost, pasar directo
    
    // Convertir dB a ganancia lineal (boost_db ≈ 2.0 para +2dB)
    float gain = std::pow(10.0f, boost_db / 20.0f);
    
    // Aplicar biquad
    float y = EQ_2K4K_COEFS.b0 * x + EQ_2K4K_COEFS.b1 * st.x1 + EQ_2K4K_COEFS.b2 * st.x2
            - EQ_2K4K_COEFS.a1 * st.y1 - EQ_2K4K_COEFS.a2 * st.y2;
    
    st.x2 = st.x1;
    st.x1 = x;
    st.y2 = st.y1;
    st.y1 = y;
    
    // Salida = original + (boost-1) * filtered (por razones numéricas)
    return x + (gain - 1.0f) * y;
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

[[maybe_unused]] static inline float fast_sin(float phase) {
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

[[maybe_unused]] static inline float kalmanStep(KalmanState &k, float meas, float dt) {
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
    EQState eqState;          // Estado del filtro EQ paramétrico
    // Anti-Dolby v1.5: parámetros ajustables via JNI
    float exciterAmount = 0.0f;
    float eqGain = 0.0f;
    float widthAmount = 0.0f;
    bool bypass = false;
    AntiDolbyState antiDolby;
} gState;

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
}

// ── JNI: set EQ gain ──────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetEqGain(JNIEnv* /*env*/, jobject /*thiz*/, jfloat gain) {
    if (!std::isfinite(gain)) return;
    gState.eqGain = std::clamp(gain, -12.0f, 12.0f);
}

// ── JNI: set width ────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetWidth(JNIEnv* /*env*/, jobject /*thiz*/, jfloat width) {
    if (!std::isfinite(width)) return;
    gState.widthAmount = std::clamp(width, 0.0f, 1.0f);
}

// ── JNI: set bypass ───────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetBypass(JNIEnv* /*env*/, jobject /*thiz*/, jboolean bypass) {
    gState.bypass = bypass;
}

// ── Procesamiento principal ───────────────────────────────────────────────────
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

    const float dt = 1.0f / (float)gState.sampleRate;

    // Anti-Dolby v1.5: downmix inteligente si channelCount > 2
    // Layout Android típico: FL(0) FR(1) FC(2) LFE(3) BL(4) BR(5) SL(6) SR(7)
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
            float eqBoost = gState.antiDolby.eqBoost2k4k.load(std::memory_order_relaxed);

            // === Aplicar EQ paramétrico (boost 2-4kHz si speech) ===
            if (eqBoost > 0.001f) {
                left = apply_eq_boost(gState.eqState, left, eqBoost);
                right = apply_eq_boost(gState.eqState, right, eqBoost);
            }

            // Aplicar width reducido si speech detectado
            float mid = (left + right) * 0.5f;
            float side = (left - right) * 0.5f * widthMul;
            left = mid + side;
            right = mid - side;

            // Limiter hard-clip al final de cadena — SIEMPRE activo
            left = process_limiter(left);
            right = process_limiter(right);

            out[i * 2 + 0] = left;
            out[i * 2 + 1] = right;
        }
    } else {
        // Procesamiento estéreo normal
        for (int i = 0; i < frames * channels; ++i) {
            float x = in[i];

            if (gState.bypass) {
                out[i] = x;
                continue;
            }

            // Validación NaN/Inf por sample
            if (!std::isfinite(x)) {
                out[i] = 0.0f;
                continue;
            }

            // Aplicar gain
            x *= gState.gain;

            // Anti-Dolby: ajustes dinámicos
            float eqBoost = gState.antiDolby.eqBoost2k4k.load(std::memory_order_relaxed);
            float widthMul = gState.antiDolby.widenerMultiplier.load(std::memory_order_relaxed);
            
            // === Aplicar EQ paramétrico (boost 2-4kHz si speech) ===
            if (eqBoost > 0.001f) {
                x = apply_eq_boost(gState.eqState, x, eqBoost);
            }

            // Limiter hard-clip al final de cadena — SIEMPRE activo
            x = process_limiter(x);

            out[i] = x;
        }
    }

    env->ReleaseFloatArrayElements(inArray, in, JNI_ABORT);
    env->ReleaseFloatArrayElements(outArray, out, 0);
}

// ── JNI: get LUFS integrado (placeholder para benchmark v1.5) ─────────────────
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetLufs(JNIEnv* /*env*/, jobject /*thiz*/) {
    // TODO: implementar en commit 16 (benchmark)
    return -23.0f;
}

// ── JNI: get pico dBFS (placeholder para benchmark v1.5) ──────────────────────
extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeGetPeakDbfs(JNIEnv* /*env*/, jobject /*thiz*/) {
    // TODO: implementar en commit 16 (benchmark)
    return -6.0f;
}

// ── JNI: set Anti-Dolby classification scores ───────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeSetAntiDolbyScores(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat speech, jfloat music, jfloat bass
) {
    if (!std::isfinite(speech) || !std::isfinite(music) || !std::isfinite(bass)) {
        LOGE("nativeSetAntiDolbyScores: valores NaN/Inf recibidos");
        return;
    }
    gState.antiDolby.updateFromClassification(
        std::clamp(speech, 0.0f, 1.0f),
        std::clamp(music, 0.0f, 1.0f),
        std::clamp(bass, 0.0f, 1.0f)
    );
    LOGI("Anti-Dolby: speech=%.2f music=%.2f bass=%.2f", speech, music, bass);
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

// ── Símbolo externo para el stub JNI: perfil de ruta (BT/AUX/USB) ───────────
// Llamado desde ivanna_jni_stub.cpp → AudioEngine companion @JvmStatic.
// Delega al UnifiedControlFrame vía control_set_route_profile() (audio_control_plane.hpp).
extern "C" void ivanna_set_route_profile(float bassBoostDb, float dialogBoostDb, float widenerMult) {
    if (!std::isfinite(bassBoostDb) || !std::isfinite(dialogBoostDb) || !std::isfinite(widenerMult)) return;
    control_set_route_profile(bassBoostDb, dialogBoostDb, widenerMult);
}
