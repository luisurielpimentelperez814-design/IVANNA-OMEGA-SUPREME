/*
 * IVANNA OMEGA SUPREME — PhaseOracle (Kalman cúbico de fase)
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * MIGRACIÓN (flanco PhaseOracle, 2026-09-08): este archivo ya NO codifica
 * su propio Kalman. El filtro vive en phase_oracle_kalman.hpp (PhaseKalman3,
 * fuente única de verdad, C++17 sin JNI, con suite host GTest). Aquí queda
 * solo el pegamento JNI + la firma C phase_oracle_velocity() que consume
 * el flanco DSP (biquad_envelope_bank.hpp).
 *
 * Bugs de raíz eliminados en esta pasada (todos verificados):
 *   1. kalmanUpdate() previo usaba P[0][0] YA actualizado para P[1][0] y
 *      P[2][0] → covarianza asimétrica, filtro no convergente. Ahora el
 *      núcleo aplica (I−KH)P con la fila 0 vieja para todas las filas.
 *   2. state[1] arrancaba en 1000.0f → transiente fantasma (cue ≈ 0.2 en
 *      silencio). Ahora kalmanInit() delega en PhaseKalman3::init() (vel=0).
 *   3. DT fijo 1/384000 sin respaldo; el núcleo usa 1/sample_rate real.
 *   4. Sin guarda NaN: una muestra corrupta envenenaba el estado. El
 *      núcleo descarta mediciones no finitas (test 1M muestras).
 *   5. Funciones decorativas ELIMINADAS (verificado: cero referencias
 *      fuera de este archivo):
 *        - stockwellTransform()  — era un memcpy, no una Stockwell
 *        - linearAutoencoder()   — devolvía una constante, no un autoencoder
 *        - takensEmbedding()     — sin llamador real
 *        - warpedFrequencyTransform() — sin llamador real
 *   6. phase_oracle_velocity() ahora devuelve la velocidad REAL del filtro
 *      (samples/sample); el puente (phase_oracle_bridge.hpp) normaliza con
 *      escala calibrada por medición (antes 1/5000 mataba el cue: ataque
 *      0→0.8 daba 0.008; ahora |vel| pico ≈ 39 → cue ≈ 0.97).
 */

#include <jni.h>
#include <cmath>

#include "phase_oracle_kalman.hpp"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace ivanna {

// alignas(64) — el núcleo vive en caché L1 sin false-sharing con el resto
// del audio thread.
struct alignas(64) KalmanCore {
    PhaseKalman3 k;
};

} // namespace ivanna

static ivanna::KalmanCore g_kalman;

__attribute__((hot))
void kalmanInit() {
    // 96 kHz = sample rate del audio thread de IVANNA. Q/R quedan con los
    // defaults calibrados del núcleo; Kotlin puede sobreescribir Q en
    // runtime vía nativeSetPhaseParameters.
    g_kalman.k.init(96000.f);
}

__attribute__((hot, flatten))
inline void kalmanPredict() {
    g_kalman.k.predict();
}

__attribute__((hot, flatten))
inline void kalmanUpdate(float measurement) {
    g_kalman.k.update(measurement);
}

__attribute__((hot, flatten))
static inline void predictSamples(const float* __restrict__ inBuf,
                                  float* __restrict__ outBuf, int n) {
    ivanna::PhaseKalman3& k = g_kalman.k;

    // 1) Filtra el bloque (predict + update por muestra)
    for (int i = 0; i < n; ++i) {
        k.tick(inBuf[i]);
    }

    // 2) Look-ahead polinómico desde el estado final (aceleración cte.)
    const float s0 = k.x[0];
    const float s1 = k.x[1];
    const float s2 = k.x[2];
    const float dt = k.dt;
    for (int i = 0; i < n; ++i) {
        const float t = (float)(i + 1) * dt;
        outBuf[i] = s0 + s1 * t + 0.5f * s2 * t * t;
    }
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
    return g_kalman.k.x[0];
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPhaseParameters(
        JNIEnv*, jobject, jfloat alpha, jfloat beta, jfloat gamma) {
    g_kalman.k.Q[0][0] = alpha;
    g_kalman.k.Q[1][1] = beta;
    g_kalman.k.Q[2][2] = gamma;
    return JNI_TRUE;
}

// ── C export for PhaseOracleBridge ──────────────────────────────────────────
// state[1] = velocidad (samples/sample) — derivada instantánea para el
// detector de transitorios de BiquadEnvelopeBank. El bridge normaliza.
extern "C" float phase_oracle_velocity() {
    return g_kalman.k.x[1];
}
