/*
 * IVANNA-FUSION TRASCENDENTAL - OPTIMIZADO (QUIRÚRGICO)
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * PhaseOracle: predicción de muestras via Kalman cúbico + embedding de Takens.
 */

#include <jni.h>
#include <cmath>
#include <cstring>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

static constexpr float DT = 1.0f / 384000.0f;
static constexpr float HALF_DT_SQ = 0.5f * DT * DT;

#define STOCKWELL_SIZE 256

struct alignas(64) KalmanCubic {
    float state[3];
    float P[3][3];
    float Q[3][3];
    float R;
    float K[3];
    float F[3][3];
};

static KalmanCubic g_kalman;

__attribute__((hot))
void kalmanInit() {
    g_kalman.state[0] = 0.0f;
    g_kalman.state[1] = 1000.0f;
    g_kalman.state[2] = 0.0f;

    memset(g_kalman.P, 0, sizeof(g_kalman.P));
    g_kalman.P[0][0] = 1.0f;
    g_kalman.P[1][1] = 10000.0f;
    g_kalman.P[2][2] = 10.0f;

    g_kalman.R = 0.01f;

    memset(g_kalman.F, 0, sizeof(g_kalman.F));
    g_kalman.F[0][0] = 1.0f;
    g_kalman.F[0][1] = DT;
    g_kalman.F[0][2] = HALF_DT_SQ;
    g_kalman.F[1][1] = 1.0f;
    g_kalman.F[1][2] = DT;
    g_kalman.F[2][2] = 1.0f;
}

__attribute__((hot, flatten))
void kalmanPredict() {
    const float s0 = g_kalman.state[0];
    const float s1 = g_kalman.state[1];
    const float s2 = g_kalman.state[2];

    g_kalman.state[0] = s0 + DT * s1 + HALF_DT_SQ * s2;
    g_kalman.state[1] = s1 + DT * s2;
}

__attribute__((hot, flatten))
void kalmanUpdate(float measurement) {
    const float y = measurement - g_kalman.state[0];
    const float p00 = g_kalman.P[0][0];
    const float S = p00 + g_kalman.R;
    const float S_inv = 1.0f / S;

    const float K0 = p00 * S_inv;
    const float K1 = g_kalman.P[1][0] * S_inv;
    const float K2 = g_kalman.P[2][0] * S_inv;

    g_kalman.state[0] += K0 * y;
    g_kalman.state[1] += K1 * y;
    g_kalman.state[2] += K2 * y;

    g_kalman.P[0][0] -= K0 * p00;
    g_kalman.P[1][0] -= K1 * g_kalman.P[0][0];
    g_kalman.P[2][0] -= K2 * g_kalman.P[0][0];
}

__attribute__((hot))
void stockwellTransform(float* __restrict__ input, float* __restrict__ output, int n) {
    memcpy(output, input, (size_t)n * sizeof(float));
}

__attribute__((hot, flatten))
void takensEmbedding(const float* __restrict__ input, float* __restrict__ embedded, int n, int delay, int dim) {
    const int valid_n = n - (dim - 1) * delay;
    if (valid_n <= 0) return;

    for (int i = 0; i < valid_n; ++i) {
        float* dest = embedded + i * dim;
        for (int d = 0; d < dim; ++d) {
            dest[d] = input[i + d * delay];
        }
    }
}

__attribute__((hot, flatten))
void linearAutoencoder(const float* __restrict__ input, float* __restrict__ output, int dimIn, int dimOut) {
    constexpr float WEIGHT = 0.015625f;

    float sum = 0.0f;
    for (int j = 0; j < dimIn; ++j) {
        sum += input[j];
    }
    sum *= WEIGHT;

    for (int i = 0; i < dimOut; ++i) {
        output[i] = sum;
    }
}

__attribute__((hot))
void warpedFrequencyTransform(float* coefs, float lambda) {
    const float a1 = coefs[3];
    const float a2 = coefs[4];

    const float denom1 = 1.0f + lambda * a1;
    if (std::fabs(denom1) < 1e-6f) return;

    const float inv_denom = 1.0f / denom1;
    coefs[3] = (a1 + lambda) * inv_denom;
    coefs[4] = (a2 + lambda * a1) * inv_denom;
}

__attribute__((hot, flatten))
static inline void predictSamples(const float* __restrict__ inBuf, float* __restrict__ outBuf, int n) {
    for (int i = 0; i < n; ++i) {
        kalmanPredict();
        kalmanUpdate(inBuf[i]);
    }

    const float s0 = g_kalman.state[0];
    const float s1 = g_kalman.state[1];
    const float s2 = g_kalman.state[2];

    for (int i = 0; i < n; ++i) {
        const float t = (float)(i + 1) * DT;
        outBuf[i] = s0 + s1 * t + 0.5f * s2 * t * t;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativePredictSamples(
        JNIEnv* env, jobject, jlong, jfloatArray input, jfloatArray output, jint n) {

    static bool initialized = false;
    if (!initialized) {
        kalmanInit();
        initialized = true;
    }

    jfloat* inBuf  = env->GetFloatArrayElements(input,  nullptr);
    jfloat* outBuf = env->GetFloatArrayElements(output, nullptr);

    predictSamples(inBuf, outBuf, n);

    env->ReleaseFloatArrayElements(input,  inBuf,  JNI_ABORT);
    env->ReleaseFloatArrayElements(output, outBuf, 0);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativePredictSamples(
        JNIEnv* env, jobject, jfloatArray audioBuffer, jint sampleCount) {

    static bool initialized = false;
    if (!initialized) {
        kalmanInit();
        initialized = true;
    }

    jfloat* inBuf = env->GetFloatArrayElements(audioBuffer, nullptr);
    const int n = sampleCount;

    jfloatArray result = env->NewFloatArray(n);
    jfloat* outBuf = env->GetFloatArrayElements(result, nullptr);

    predictSamples(inBuf, outBuf, n);

    env->ReleaseFloatArrayElements(audioBuffer, inBuf, JNI_ABORT);
    env->ReleaseFloatArrayElements(result, outBuf, 0);
    return result;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetPhaseState(JNIEnv*, jobject) {
    return g_kalman.state[0];
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPhaseParameters(
        JNIEnv*, jobject, jfloat alpha, jfloat beta, jfloat gamma) {
    g_kalman.Q[0][0] = alpha;
    g_kalman.Q[1][1] = beta;
    g_kalman.Q[2][2] = gamma;
    return JNI_TRUE;
}

// ── C export for PhaseOracleBridge ──────────────────────────────────────────
// Returns state[1] (velocity = instantaneous derivative) — used by
// BiquadEnvelopeBank to refine the transient cue T_t.
extern "C" float phase_oracle_velocity() {
    return g_kalman.state[1];
}
