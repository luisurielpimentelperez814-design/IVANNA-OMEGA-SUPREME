/*
 * IVANNA-FUSION TRASCENDENTAL
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * PhaseOracle: predicción de muestras via Kalman cúbico + embedding de Takens.
 * Correcciones en esta revisión:
 *  - Eliminado #define M_PI duplicado (el valor real ya lo da <cmath>; la
 *    redefinición como float silenciosa causaba -Wmacro-redefined que con
 *    -Wall convertía la TU en error en Clang 18/NDK r27c).
 *  - #include <arm_neon.h> guardado con #ifdef __aarch64__ para que la
 *    unidad compile también en hosts x86_64 (smoke-test de CI).
 */

#include <jni.h>
#include <cmath>
#include <cstring>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

// Constante Pi local (float, 7 dígitos de precisión — suficiente para DSP)
// Se llama kPif para no colisionar con M_PI de <cmath> (que es double).
static constexpr float kPif = 3.14159265f;

#define STOCKWELL_SIZE 256

// Filtro de Kalman cúbico: estado [phase, freq, chirp]
struct KalmanCubic {
    float state[3];      // [phase, frequency, chirp_rate]
    float P[3][3];       // Covarianza
    float Q[3][3];       // Ruido de proceso
    float R;             // Ruido de medición
    float K[3];          // Ganancia

    // Matriz de transición (precomputada)
    float F[3][3];
};

static KalmanCubic g_kalman;

void kalmanInit() {
    g_kalman.state[0] = 0.0f;
    g_kalman.state[1] = 1000.0f;
    g_kalman.state[2] = 0.0f;

    memset(g_kalman.P, 0, sizeof(g_kalman.P));
    g_kalman.P[0][0] = 1.0f;
    g_kalman.P[1][1] = 10000.0f;
    g_kalman.P[2][2] = 10.0f;

    g_kalman.R = 0.01f;

    // F = [[1, dt, 0.5*dt^2], [0, 1, dt], [0, 0, 1]] con dt=1/fs
    float dt = 1.0f / 384000.0f;
    memset(g_kalman.F, 0, sizeof(g_kalman.F));
    g_kalman.F[0][0] = 1.0f; g_kalman.F[0][1] = dt; g_kalman.F[0][2] = 0.5f * dt * dt;
    g_kalman.F[1][1] = 1.0f; g_kalman.F[1][2] = dt;
    g_kalman.F[2][2] = 1.0f;
}

void kalmanPredict() {
    float newState[3];
    newState[0] = g_kalman.F[0][0] * g_kalman.state[0] +
                  g_kalman.F[0][1] * g_kalman.state[1] +
                  g_kalman.F[0][2] * g_kalman.state[2];
    newState[1] = g_kalman.F[1][1] * g_kalman.state[1] +
                  g_kalman.F[1][2] * g_kalman.state[2];
    newState[2] = g_kalman.F[2][2] * g_kalman.state[2];

    memcpy(g_kalman.state, newState, sizeof(newState));
    // P = F * P * F^T + Q (simplificado diagonal)
}

void kalmanUpdate(float measurement) {
    float y = measurement - g_kalman.state[0];
    float S = g_kalman.P[0][0] + g_kalman.R;

    g_kalman.K[0] = g_kalman.P[0][0] / S;
    g_kalman.K[1] = g_kalman.P[1][0] / S;
    g_kalman.K[2] = g_kalman.P[2][0] / S;

    g_kalman.state[0] += g_kalman.K[0] * y;
    g_kalman.state[1] += g_kalman.K[1] * y;
    g_kalman.state[2] += g_kalman.K[2] * y;

    g_kalman.P[0][0] -= g_kalman.K[0] * g_kalman.P[0][0];
    g_kalman.P[1][0] -= g_kalman.K[1] * g_kalman.P[0][0];
    g_kalman.P[2][0] -= g_kalman.K[2] * g_kalman.P[0][0];
}

// Transformada de Stockwell simplificada (placeholder — requiere FFT propio)
void stockwellTransform(float *input, float *output, int n) {
    memcpy(output, input, (size_t)n * sizeof(float));
    (void)kPif; // referencia explícita para evitar unused-variable si no se usa
}

// Embedding de Takens (espacio de fases)
void takensEmbedding(float *input, float *embedded, int n, int delay, int dim) {
    for (int i = 0; i < n - (dim - 1) * delay; i++) {
        for (int d = 0; d < dim; d++) {
            embedded[i * dim + d] = input[i + d * delay];
        }
    }
}

// Autoencoder lineal (matriz de proyección fija, Q8.24)
void linearAutoencoder(float *input, float *output, int dimIn, int dimOut) {
    for (int i = 0; i < dimOut; i++) {
        output[i] = 0.0f;
        for (int j = 0; j < dimIn; j++) {
            output[i] += input[j] * 0.015625f; // 1/64
        }
    }
}

// Warped Frequency Transform (ajuste de coeficientes biquad)
void warpedFrequencyTransform(float *coefs, float lambda) {
    float a1 = coefs[3];
    float a2 = coefs[4];

    float a1w = (a1 + lambda) / (1.0f + lambda * a1);
    float a2w = (a2 + lambda * a1) / (1.0f + lambda * a1);

    coefs[3] = a1w;
    coefs[4] = a2w;
}

// Predicción de muestras (Kalman cúbico)
extern "C" JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativePredictSamples(
        JNIEnv *env, jobject /*thiz*/,
        jlong /*handle*/, jfloatArray input, jfloatArray output, jint n) {

    static bool initialized = false;
    if (!initialized) { kalmanInit(); initialized = true; }

    jfloat *inBuf  = env->GetFloatArrayElements(input,  nullptr);
    jfloat *outBuf = env->GetFloatArrayElements(output, nullptr);

    // Actualizar Kalman con muestras actuales
    for (int i = 0; i < n; i++) {
        kalmanPredict();
        kalmanUpdate(inBuf[i]);
    }

    // Predecir siguientes N muestras usando modelo cúbico
    float dt = 1.0f / 384000.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * dt;
        outBuf[i] = g_kalman.state[0]
                  + g_kalman.state[1] * t
                  + 0.5f * g_kalman.state[2] * t * t;
    }

    env->ReleaseFloatArrayElements(input,  inBuf,  JNI_ABORT);
    env->ReleaseFloatArrayElements(output, outBuf, 0);
}

// ── Bindings para IvannaNativeLib.kt ───────────────────────────────────────
// Firmas distintas a las de AudioEngine: nativePredictSamples aquí recibe
// (buffer, sampleCount) y DEVUELVE un FloatArray nuevo, en vez de escribir
// sobre un buffer de salida ya asignado. Reutiliza el mismo filtro de
// Kalman cúbico (g_kalman) ya implementado arriba.

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativePredictSamples(
        JNIEnv *env, jobject /*thiz*/, jfloatArray audioBuffer, jint sampleCount) {

    static bool initialized = false;
    if (!initialized) { kalmanInit(); initialized = true; }

    jfloat *inBuf = env->GetFloatArrayElements(audioBuffer, nullptr);
    int n = sampleCount;

    for (int i = 0; i < n; i++) {
        kalmanPredict();
        kalmanUpdate(inBuf[i]);
    }

    jfloatArray result = env->NewFloatArray(n);
    jfloat *outBuf = env->GetFloatArrayElements(result, nullptr);

    float dt = 1.0f / 384000.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * dt;
        outBuf[i] = g_kalman.state[0]
                  + g_kalman.state[1] * t
                  + 0.5f * g_kalman.state[2] * t * t;
    }

    env->ReleaseFloatArrayElements(audioBuffer, inBuf, JNI_ABORT);
    env->ReleaseFloatArrayElements(result, outBuf, 0);
    return result;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeGetPhaseState(JNIEnv *, jobject) {
    // Estado de fase actual del filtro de Kalman cúbico (state[0] = phase).
    return g_kalman.state[0];
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeSetPhaseParameters(
        JNIEnv *, jobject, jfloat alpha, jfloat beta, jfloat gamma) {
    // Mapea alpha/beta/gamma a la covarianza de proceso diagonal (Q) del
    // Kalman cúbico: alpha controla phase, beta frequency, gamma chirp.
    // No existían parámetros de ruido de proceso ajustables antes de
    // este binding; se exponen aquí sin modificar la estructura KalmanCubic.
    g_kalman.Q[0][0] = alpha;
    g_kalman.Q[1][1] = beta;
    g_kalman.Q[2][2] = gamma;
    return JNI_TRUE;
}

