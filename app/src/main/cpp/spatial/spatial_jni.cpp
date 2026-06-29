/*
 * spatial_jni.cpp
 * JNI bridge for IVANNA OMEGA SUPREME Spatial Engine
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */

#include <jni.h>
#include <cstring>
#include <cmath>
#include <string>
#include <android/log.h>
#include "spatial_engine.h"
#include "room_model.h"

#define LOG_TAG "SpatialJNI"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static SpatialState g_spatialState;
static bool g_spatialInitialized = false;
static int g_spatialSampleRate = 48000;
static int g_spatialBufferSize = 1024;

// Local head-shadow filter (isolated from spatial_engine.cpp globals)
struct HeadShadowFilter {
    float z1 = 0.f;
    inline float process(float x, float cutoff) {
        cutoff = std::max(0.01f, std::min(0.99f, cutoff));
        float y = x - z1;
        z1 = cutoff * z1 + (1.0f - cutoff) * x;
        return y;
    }
    void reset() { z1 = 0.f; }
};

static void hrtfProcessChannel(const float* __restrict__ in,
                                float* __restrict__ out,
                                int len,
                                float angle_deg,
                                bool is_left) {
    if (!std::isfinite(angle_deg)) angle_deg = 0.0f;
    while (angle_deg > 180.0f) angle_deg -= 360.0f;
    while (angle_deg < -180.0f) angle_deg += 360.0f;

    const float angle_rad = angle_deg * 3.14159265f / 180.0f;

    float delay_f = 0.5f * sinf(angle_rad) * 20.0f;
    if (!std::isfinite(delay_f)) delay_f = 0.0f;
    const int delay = static_cast<int>(delay_f);

    float cutoff = 0.8f + 0.2f * cosf(angle_rad);
    if (!std::isfinite(cutoff)) cutoff = 0.8f;
    cutoff = std::max(0.01f, std::min(0.99f, cutoff));

    HeadShadowFilter hp;
    for (int i = 0; i < len; ++i) {
        int idx = i - delay;
        float sample = (idx >= 0 && idx < len) ? in[idx] : in[i] * 0.5f;
        float filtered = hp.process(sample, cutoff);
        out[i] = filtered;
    }
}

static float computePStar() {
    float n_e = std::isfinite(g_spatialState.n_energy) ? g_spatialState.n_energy : 0.0f;
    float o_e = std::isfinite(g_spatialState.omega_energy) ? g_spatialState.omega_energy : 0.0f;
    float mu_f = std::isfinite((float)g_spatialState.mu) ? (float)g_spatialState.mu / 1000.0f : 1.0f;
    if (mu_f < -0.99f) mu_f = -0.99f;

    float p_star = (n_e + mu_f * o_e) / (1.0f + mu_f);
    if (!std::isfinite(p_star)) p_star = 1.0f;
    if (p_star < 0.01f) p_star = 0.01f;
    if (p_star > 2.0f) p_star = 2.0f;
    return p_star;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitSpatialEngine(
    JNIEnv*, jobject, jint sr, jint bs) {
    g_spatialSampleRate = sr;
    g_spatialBufferSize = bs;
    spatial_init(&g_spatialState);
    g_spatialInitialized = true;
    ALOGI("Spatial engine initialized @ %d Hz, buffer=%d", sr, bs);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeRenderSpatialBlock(
    JNIEnv* env, jobject,
    jfloatArray inputBuffer,
    jfloatArray outL,
    jfloatArray outR,
    jint posX, jint posY, jint posZ, jint mu) {

    if (!g_spatialInitialized) {
        ALOGE("Spatial engine not initialized");
        return 0;
    }

    jsize n = env->GetArrayLength(inputBuffer);
    if (n <= 0) return 0;

    jfloat* inBuf = env->GetFloatArrayElements(inputBuffer, nullptr);
    if (!inBuf) return 0;

    jfloat* lBuf = env->GetFloatArrayElements(outL, nullptr);
    jfloat* rBuf = env->GetFloatArrayElements(outR, nullptr);
    if (!lBuf || !rBuf) {
        if (inBuf) env->ReleaseFloatArrayElements(inputBuffer, inBuf, JNI_ABORT);
        if (lBuf) env->ReleaseFloatArrayElements(outL, lBuf, 0);
        if (rBuf) env->ReleaseFloatArrayElements(outR, rBuf, 0);
        return 0;
    }

    // Update spatial state
    g_spatialState.posX = static_cast<int32_t>(posX);
    g_spatialState.posY = static_cast<int32_t>(posY);
    g_spatialState.posZ = static_cast<int32_t>(posZ);
    g_spatialState.mu = static_cast<int16_t>(mu);

    // Process left and right channels
    hrtfProcessChannel(inBuf, lBuf, n, (float)posX, true);
    hrtfProcessChannel(inBuf, rBuf, n, -(float)posX, false);

    // Apply consensus scaling (p*)
    float p_star = computePStar();
    for (int i = 0; i < n; ++i) {
        lBuf[i] *= p_star;
        rBuf[i] *= p_star;
    }

    // Apply light room reverb (FDN) for spatial depth
    constexpr int MAX_BLOCK = 2048;
    if (n <= MAX_BLOCK) {
        alignas(16) float revL[MAX_BLOCK];
        alignas(16) float revR[MAX_BLOCK];
        apply_reverb_f(lBuf, revL, n, 50, 0.3f);
        apply_reverb_f(rBuf, revR, n, 50, 0.3f);
        const float revMix = 0.25f;
        for (int i = 0; i < n; ++i) {
            lBuf[i] = lBuf[i] * (1.0f - revMix) + revL[i] * revMix;
            rBuf[i] = rBuf[i] * (1.0f - revMix) + revR[i] * revMix;
        }
    }

    env->ReleaseFloatArrayElements(inputBuffer, inBuf, JNI_ABORT);
    env->ReleaseFloatArrayElements(outL, lBuf, 0);
    env->ReleaseFloatArrayElements(outR, rBuf, 0);

    return n;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeReleaseSpatialEngine(JNIEnv*, jobject) {
    g_spatialInitialized = false;
    memset(&g_spatialState, 0, sizeof(g_spatialState));
    ALOGI("Spatial engine released");
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetSpatialState(JNIEnv* env, jobject) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"posX\":%d,\"posY\":%d,\"posZ\":%d,\"mu\":%d,"
        "\"n_energy\":%.4f,\"omega_energy\":%.4f,"
        "\"initialized\":%s}",
        g_spatialState.posX, g_spatialState.posY, g_spatialState.posZ,
        g_spatialState.mu,
        g_spatialState.n_energy, g_spatialState.omega_energy,
        g_spatialInitialized ? "true" : "false");
    return env->NewStringUTF(buf);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSpatialParams(
    JNIEnv* env, jobject, jstring params) {
    if (!params) return JNI_FALSE;
    const char* str = env->GetStringUTFChars(params, nullptr);
    if (!str) return JNI_FALSE;

    // Simple JSON parser for key-value pairs
    const char* p = str;
    while (*p) {
        if (strncmp(p, "\"mu\":", 5) == 0) {
            p += 5;
            while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
            g_spatialState.mu = static_cast<int16_t>(atoi(p));
        } else if (strncmp(p, "\"n_energy\":", 11) == 0) {
            p += 11;
            while (*p && (*p < '0' || *p > '9') && *p != '-' && *p != '.') p++;
            g_spatialState.n_energy = strtof(p, nullptr);
        } else if (strncmp(p, "\"omega_energy\":", 15) == 0) {
            p += 15;
            while (*p && (*p < '0' || *p > '9') && *p != '-' && *p != '.') p++;
            g_spatialState.omega_energy = strtof(p, nullptr);
        } else if (strncmp(p, "\"posX\":", 7) == 0) {
            p += 7;
            while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
            g_spatialState.posX = atoi(p);
        } else if (strncmp(p, "\"posY\":", 7) == 0) {
            p += 7;
            while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
            g_spatialState.posY = atoi(p);
        } else if (strncmp(p, "\"posZ\":", 7) == 0) {
            p += 7;
            while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
            g_spatialState.posZ = atoi(p);
        }
        p++;
    }

    env->ReleaseStringUTFChars(params, str);
    ALOGI("Spatial params updated: mu=%d n_energy=%.4f omega_energy=%.4f",
          g_spatialState.mu, g_spatialState.n_energy, g_spatialState.omega_energy);
    return JNI_TRUE;
}

} // extern "C"
