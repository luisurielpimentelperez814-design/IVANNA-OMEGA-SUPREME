/*
 * pi_lstm_bridge_jni.cpp
 * JNI bindings for com.ivanna.omega.neuromorphic.PiLstmBridge (Kotlin object).
 *
 * FIX: Resolves UnsatisfiedLinkError on
 *   Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeInit
 * and the rest of the PiLstmBridge external methods. Previously these symbols
 * were not exported by libivanna_omega.so, so the static initializer of the
 * Kotlin `object PiLstmBridge` crashed when DashboardScreen first read
 * `PiLstmBridge.isReady` (MainActivity.kt:169).
 *
 * This file is intentionally small and self-contained. It owns its own
 * PILSTMMilenioEngine instance (g_piLstmBridge), independent from the one
 * used by IvannaNativeLib/DSPBridge, so the two surfaces can coexist without
 * fighting over state. If you want to share state, swap g_piLstmBridge for
 * the existing g_piLstm symbol (extern it from ivanna_omega_jni.cpp).
 *
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */

#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ctime>

#include "../neuromorphic/pi_lstm_milenio.hpp"

#define LOG_TAG "IVANNA-JNI-PILSTM"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using ivanna::PILSTMMilenioEngine;

// Dedicated engine for the PiLstmBridge Kotlin surface.
static PILSTMMilenioEngine     g_piLstmBridge;
static std::atomic<bool>       g_piLstmBridge_ready{false};

// Estado del estimador de residual — declarado aquí (antes del extern "C")
// para que nativeSetNPMax (que invalida g_residual_seed) pueda usarlo sin
// necesidad de forward-declaration.
static std::atomic<float> g_residual_ema{0.0f};
static float              g_prev_h        = 0.0f;
static float              g_prev_c        = 0.0f;
static bool               g_residual_seed = false;
static int64_t            g_prev_ns       = 0;

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeInit(JNIEnv*, jobject) {
    g_piLstmBridge.reset();
    g_piLstmBridge_ready.store(true, std::memory_order_release);
    LOGI("PiLstmBridge.nativeInit: PI-LSTM Milenio engine ready");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetAlpha(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_alpha(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetBeta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_beta(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetGamma(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_gamma(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetDelta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_delta(v);
}

// ── nativeSetEta — amortiguamiento η de la ODE (rango 0..5) ─────────────────
// Faltaba: Kotlin declara nativeSetEta() en PiLstmBridge pero no había símbolo.
// η controla la tasa de disipación del estado oculto entre pasos RK4 — a
// mayor η, más agresivo el decaimiento; 0 = sin disipación.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetEta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_eta(v);
}

// ── nativeSetNPMax — techo neuroplástico NP_max (rango 0.1..10) ─────────────
// Faltaba: Kotlin declara nativeSetNPMax() en PiLstmBridge pero no había símbolo.
// NP_max define el rango de saturación del estado oculto h ∈ [-NP_max, NP_max].
// También es el denominador de la saturación normalizada publicada por nativeGetNpSat.
// Al cambiarlo se invalida la semilla del estimador de residual (dt grande próxima
// consulta → guarda automáticamente un nuevo punto de referencia).
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetNPMax(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_np_max(v);
    // Invalidar la semilla del estimador de residual: con un NP_max distinto,
    // la normalización cambia y el h_pred anterior ya no es comparable.
    g_residual_seed = false;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHarmonicGain(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_harmonic_gain(v);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHrtfEnabled(JNIEnv*, jobject, jboolean en) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_piLstmBridge.set_hrtf_enabled(en == JNI_TRUE);
}

/*
 * TELEMETRÍA REAL (sustituye el placeholder anterior).
 *
 * nativeGetNpSat  -> saturación neuroplástica normalizada:
 *                    |h| / NP_max, es decir cuánto del techo del estado
 *                    oculto está consumido ahora mismo (0..1). El valor
 *                    crudo |h| no era comparable entre configuraciones
 *                    porque NP_max es ajustable.
 *
 * nativeGetError  -> residual físico REAL de la ODE continua: se compara el
 *                    estado observado h(t) contra la predicción de un paso
 *                    del propio modelo, h_pred = h(t-Δt) + Δt·dh/dt evaluada
 *                    en el instante anterior (Euler explícito de referencia
 *                    frente al RK4 del integrador). La discrepancia entre
 *                    ambos es exactamente el error de truncamiento local del
 *                    integrador + la energía inyectada por la entrada, que es
 *                    la métrica que la UI necesita para saber si el motor
 *                    está siguiendo la señal o divergiendo.
 *                    Se suaviza con EMA (τ ≈ 0.5 s) y se normaliza por NP_max
 *                    para que quede en un rango estable 0..1.
 */

// Estado del estimador de residual (solo tocado desde el hilo de UI que
// consulta la telemetría; atómico para publicar hacia cualquier lector).
// Declarado al inicio del archivo (antes del extern "C") para evitar uso
// antes de la declaración en nativeSetNPMax.

static inline int64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static void update_residual() {
    auto& cell = g_piLstmBridge.lstm;
    const float h   = std::isfinite(cell.h) ? cell.h : 0.0f;
    const float c   = std::isfinite(cell.c) ? cell.c : 0.0f;
    const int64_t t = now_ns();

    if (!g_residual_seed) {
        g_prev_h = h; g_prev_c = c; g_prev_ns = t;
        g_residual_seed = true;
        return;
    }

    float dt = (float)(t - g_prev_ns) * 1e-9f;
    g_prev_ns = t;
    // Ventana útil: por debajo de 1 ms el ruido numérico domina, por encima
    // de 250 ms la extrapolación de un paso deja de tener sentido físico.
    if (!(dt > 1e-3f && dt < 0.25f)) { g_prev_h = h; g_prev_c = c; return; }

    // Predicción de referencia con la propia dinámica del modelo evaluada
    // en el estado ANTERIOR. x = 0 porque entre consultas de telemetría no
    // conocemos la entrada: el residual mide entonces la deriva libre del
    // sistema frente a su trayectoria observada (drift + drive externo).
    const float dh = cell.dh_dt(g_prev_c, g_prev_h, 0.0f);
    const float h_pred = g_prev_h + dh * dt;

    float npmax = cell.NP_max;
    if (!(npmax > 1e-6f) || !std::isfinite(npmax)) npmax = 1.0f;

    float residual = std::fabs(h - h_pred) / npmax;
    if (!std::isfinite(residual)) residual = 0.0f;
    if (residual > 4.0f) residual = 4.0f;

    // EMA con τ ≈ 0.5 s independiente de la cadencia de muestreo
    const float tau   = 0.5f;
    const float alpha = 1.0f - std::exp(-dt / tau);
    float prev = g_residual_ema.load(std::memory_order_relaxed);
    float next = prev + alpha * (residual - prev);
    if (!std::isfinite(next)) next = 0.0f;
    g_residual_ema.store(next, std::memory_order_relaxed);

    g_prev_h = h; g_prev_c = c;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetNpSat(JNIEnv*, jobject) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return 0.0f;
    update_residual();
    const auto& cell = g_piLstmBridge.lstm;
    float h = cell.h;
    if (!std::isfinite(h)) return 0.0f;
    float npmax = cell.NP_max;
    if (!(npmax > 1e-6f) || !std::isfinite(npmax)) npmax = 1.0f;
    float sat = std::fabs(h) / npmax;
    if (!std::isfinite(sat)) return 0.0f;
    return sat > 1.0f ? 1.0f : sat;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetError(JNIEnv*, jobject) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return 0.0f;
    update_residual();
    float e = g_residual_ema.load(std::memory_order_relaxed);
    if (!std::isfinite(e)) return 0.0f;
    return e > 1.0f ? 1.0f : e;
}

/*
 * Reset explícito del estimador de residual — necesario al cambiar de pista
 * o al reinicializar el motor, si no la EMA arrastra el transitorio anterior.
 */
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeResetTelemetry(JNIEnv*, jobject) {
    g_residual_ema.store(0.0f, std::memory_order_relaxed);
    g_residual_seed = false;
    g_prev_h = g_prev_c = 0.0f;
    g_prev_ns = 0;
    LOGI("PiLstmBridge.nativeResetTelemetry: estimador de residual reiniciado");
}

} // extern "C"
