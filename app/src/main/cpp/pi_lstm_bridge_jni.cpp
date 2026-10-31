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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "../neuromorphic/pi_lstm_milenio.hpp"

#define LOG_TAG "IVANNA-JNI-PILSTM"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using ivanna::PILSTMMilenioEngine;

// Dedicated engine for the PiLstmBridge Kotlin surface.
static PILSTMMilenioEngine     g_piLstmBridge;
static std::atomic<bool>       g_piLstmBridge_ready{false};
// The bridge is process-global and has two native audio entry points. Never
// let concurrent callbacks mutate the same RK4 state; a contending callback
// skips this auxiliary stage rather than blocking the real-time thread.
static std::atomic_flag g_piLstmBridge_owner = ATOMIC_FLAG_INIT;

// Residual telemetry is published atomically to JNI readers; its mutable
// estimator state remains exclusively owned by the audio callback.
static std::atomic<float> g_residual_ema{0.0f};
static std::atomic<float> g_published_np_sat{0.0f};

// JNI/control threads publish requested values only. The audio callback is the
// sole owner that applies them to g_piLstmBridge and advances RK4 state.
static_assert(std::atomic<float>::is_always_lock_free,
              "Pi-LSTM RT parameter mailbox requires lock-free float atomics");
static std::atomic<float> g_pending_alpha{1.0f};
static std::atomic<float> g_pending_beta{1.0f};
static std::atomic<float> g_pending_gamma{1.0f};
static std::atomic<float> g_pending_eta{1.0f};
static std::atomic<float> g_pending_np_max{1.0f};
static std::atomic<float> g_pending_harmonic_gain{0.3f};
static std::atomic<bool> g_pending_hrtf_enabled{true};
static std::atomic<bool> g_reset_requested{false};
static std::atomic<bool> g_telemetry_reset_requested{false};

// These are audio-thread-owned; no JNI/control thread reads or writes them.
static float g_prev_h = 0.0f;
static float g_prev_c = 0.0f;
static bool g_residual_seed = false;

extern "C" {

// Control/JNI never touches the mutable engine. Pending values are consumed
// here, on the single audio-thread owner, without locks or allocation.
static void apply_pending_controls() noexcept {
    if (g_reset_requested.exchange(false, std::memory_order_acq_rel)) {
        auto& e = g_piLstmBridge;
        e.up.head = 0;
        std::memset(e.up.dL, 0, sizeof(e.up.dL));
        std::memset(e.up.dR, 0, sizeof(e.up.dR));
        e.lstm.reset();
        e.hrtf.reset();
        std::memset(e.inL, 0, sizeof(e.inL));
        std::memset(e.inR, 0, sizeof(e.inR));
        std::memset(e.upL, 0, sizeof(e.upL));
        std::memset(e.upR, 0, sizeof(e.upR));
        std::memset(e.outL, 0, sizeof(e.outL));
        std::memset(e.outR, 0, sizeof(e.outR));
        e.adapt_counter = 0;
        g_telemetry_reset_requested.store(true, std::memory_order_release);
    }
    g_piLstmBridge.set_alpha(g_pending_alpha.load(std::memory_order_relaxed));
    g_piLstmBridge.set_beta(g_pending_beta.load(std::memory_order_relaxed));
    g_piLstmBridge.set_gamma(g_pending_gamma.load(std::memory_order_relaxed));
    g_piLstmBridge.set_eta(g_pending_eta.load(std::memory_order_relaxed));
    g_piLstmBridge.set_np_max(g_pending_np_max.load(std::memory_order_relaxed));
    g_piLstmBridge.set_harmonic_gain(g_pending_harmonic_gain.load(std::memory_order_relaxed));
    g_piLstmBridge.set_hrtf_enabled(g_pending_hrtf_enabled.load(std::memory_order_relaxed));
}

// Called only by the audio-thread owner after advancing RK4. dt is derived
// from the exact number of integration steps; no wall-clock query is needed.
static void update_residual_rt(float dt) noexcept {
    auto& cell = g_piLstmBridge.lstm;
    if (g_telemetry_reset_requested.exchange(false, std::memory_order_acq_rel)) {
        g_residual_ema.store(0.0f, std::memory_order_relaxed);
        g_published_np_sat.store(0.0f, std::memory_order_relaxed);
        g_prev_h = cell.h;
        g_prev_c = cell.c;
        g_residual_seed = true;
    }
    const float h = std::isfinite(cell.h) ? cell.h : 0.0f;
    const float c = std::isfinite(cell.c) ? cell.c : 0.0f;
    float npmax = cell.NP_max;
    if (!(npmax > 1e-6f) || !std::isfinite(npmax)) npmax = 1.0f;
    float sat = std::fabs(h) / npmax;
    if (!std::isfinite(sat)) sat = 0.0f;
    g_published_np_sat.store(std::min(1.0f, sat), std::memory_order_relaxed);

    if (!g_residual_seed) {
        g_prev_h = h;
        g_prev_c = c;
        g_residual_seed = true;
        return;
    }
    if (!(dt > 1e-6f && dt < 0.25f)) {
        g_prev_h = h;
        g_prev_c = c;
        return;
    }
    const float dh = cell.dh_dt(g_prev_c, g_prev_h, 0.0f);
    const float h_pred = g_prev_h + dh * dt;
    float residual = std::fabs(h - h_pred) / npmax;
    if (!std::isfinite(residual)) residual = 0.0f;
    residual = std::min(4.0f, residual);
    constexpr float tau = 0.5f;
    const float alpha = 1.0f - std::exp(-dt / tau);
    const float prev = g_residual_ema.load(std::memory_order_relaxed);
    float next = prev + alpha * (residual - prev);
    if (!std::isfinite(next)) next = 0.0f;
    g_residual_ema.store(next, std::memory_order_relaxed);
    g_prev_h = h;
    g_prev_c = c;
}

// Feed the engine from the live audio path. State and DSP parameters have one
// owner: this callback. UI/JNI reads are atomic snapshots; writes are mailboxes.
void ivanna_pilstm_bridge_tick_block(const float* inL, const float* inR, int frames, float sampleRate) noexcept {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire) || !inL || !inR || frames <= 0) return;
    if (g_piLstmBridge_owner.test_and_set(std::memory_order_acquire)) return;
    apply_pending_controls();
    const float sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
    const int step = std::max(1, frames / 16);
    const float dt = static_cast<float>(step) / sr;
    int steps = 0;
    for (int i = 0; i < frames; i += step) {
        const float mono = 0.5f * (inL[i] + inR[i]);
        if (std::isfinite(mono)) g_piLstmBridge.lstm.rk4_step(mono, dt);
        ++steps;
    }
    update_residual_rt(static_cast<float>(steps) * dt);
    g_piLstmBridge_owner.clear(std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeInit(JNIEnv*, jobject) {
    g_reset_requested.store(true, std::memory_order_release);
    g_telemetry_reset_requested.store(true, std::memory_order_release);
    g_piLstmBridge_ready.store(true, std::memory_order_release);
    LOGI("PiLstmBridge.nativeInit: PI-LSTM Milenio engine ready");
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetAlpha(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_alpha.store(std::isfinite(v) ? std::clamp(v, 0.01f, 20.0f) : 1.0f, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetBeta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_beta.store(std::isfinite(v) ? std::clamp(v, 0.01f, 20.0f) : 1.0f, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetGamma(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_gamma.store(std::isfinite(v) ? std::clamp(v, 0.01f, 20.0f) : 1.0f, std::memory_order_relaxed);
}

// ── nativeSetEta — amortiguamiento η de la ODE (rango 0..5) ─────────────────
// Faltaba: Kotlin declara nativeSetEta() en PiLstmBridge pero no había símbolo.
// η controla la tasa de disipación del estado oculto entre pasos RK4 — a
// mayor η, más agresivo el decaimiento; 0 = sin disipación.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetEta(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_eta.store(std::isfinite(v) ? std::clamp(v, 0.0f, 5.0f) : 1.0f, std::memory_order_relaxed);
}

// ── nativeSetNPMax — techo neuroplástico NP_max (rango 0.1..10) ─────────────
// Faltaba: Kotlin declara nativeSetNPMax() en PiLstmBridge pero no había símbolo.
// NP_max define el rango de saturación del estado oculto h ∈ [-NP_max, NP_max].
// También es el denominador de la saturación normalizada publicada por nativeGetNpSat.
// Al cambiarlo se solicita al propietario RT reiniciar la semilla del residual
// en el siguiente bloque, sin tocar estado mutable desde el hilo JNI.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetNPMax(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_np_max.store(std::isfinite(v) ? std::clamp(v, 0.1f, 10.0f) : 1.0f, std::memory_order_relaxed);
    // RT owner invalidates/reseeds residual on its next block.
    g_telemetry_reset_requested.store(true, std::memory_order_release);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHarmonicGain(JNIEnv*, jobject, jfloat v) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_harmonic_gain.store(std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.3f, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeSetHrtfEnabled(JNIEnv*, jobject, jboolean en) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return;
    g_pending_hrtf_enabled.store(en == JNI_TRUE, std::memory_order_relaxed);
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

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetNpSat(JNIEnv*, jobject) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return 0.0f;
    const float sat = g_published_np_sat.load(std::memory_order_relaxed);
    if (!std::isfinite(sat)) return 0.0f;
    return std::clamp(sat, 0.0f, 1.0f);
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_neuromorphic_PiLstmBridge_nativeGetError(JNIEnv*, jobject) {
    if (!g_piLstmBridge_ready.load(std::memory_order_acquire)) return 0.0f;
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
    g_telemetry_reset_requested.store(true, std::memory_order_release);
    LOGI("PiLstmBridge.nativeResetTelemetry: reset queued for audio-thread owner");
}

} // extern "C"
