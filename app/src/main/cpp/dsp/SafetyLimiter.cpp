#include "../include/SafetyLimiter.h"
#include <algorithm>
#include <cmath>  // std::max — el NDK (libc++) no lo incluye transitivamente
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna {

// ── Constantes del limiter (TAREA 5) ────────────────────────────────────────
namespace {
    constexpr float kLookaheadMs = 5.0f;    // lookahead
    constexpr float kReleaseMs   = 50.0f;   // release suave
    constexpr float kKneeRatio   = 0.1f;    // soft-knee 10:1 sobre threshold
} // namespace

void SafetyLimiter::setParams(float threshold, float ceiling) {
    m_threshold = threshold;
    m_ceiling   = ceiling;
}

void SafetyLimiter::setSampleRate(float sampleRate) {
    if (sampleRate > 8000.f) m_sampleRate = sampleRate;
    // Dimensionar delay line del lookahead fuera del hot path.
    const int len = (int)(m_sampleRate * kLookaheadMs / 1000.f);
    if (len != m_delayLen && len > 0) {
        m_delayLen = len;
        m_delayL.assign((size_t)m_delayLen, 0.f);
        m_delayR.assign((size_t)m_delayLen, 0.f);
        m_delayWrite = 0;
    }
    // Coeficiente de release (decaimiento exponencial por muestra).
    m_releaseCoef = std::exp(-1.0f / (m_sampleRate * kReleaseMs / 1000.f));
}

// Ganancia objetivo (<=1) para un peak lineal dado. Soft-knee: por encima del
// threshold se reduce a ratio 10:1 hasta el ceiling; por debajo, ganancia 1.
float SafetyLimiter::computeGainForPeak(float peakLin) const {
    if (peakLin <= m_threshold) return 1.0f;
    const float excess = peakLin - m_threshold;

    // Curva suave hacia ceiling para reducir THD.
    // Evita que el knee genere una discontinuidad fuerte.
    float limited = m_threshold +
                    excess / (1.0f + excess * 8.0f);

    if (limited > m_ceiling)
        limited = m_ceiling;

    return (peakLin > 1e-9f) ? (limited / peakLin) : 1.0f;
}

float SafetyLimiter::limitSample(float x) {
    // Camino escalar de respaldo (tail). El comportamiento audible vive en
    // process() (lookahead + release); esto solo garantiza que una muestra
    // aislada nunca salga del rango [-1, 1].
    if (!std::isfinite(x)) {
        m_clipCount.fetch_add(1, std::memory_order_relaxed);
        return 0.0f;
    }
    const float ax = std::fabs(x);
    if (ax <= m_threshold) return std::clamp(x, -1.0f, 1.0f);
    const float sign  = x < 0.0f ? -1.0f : 1.0f;
    float excess = ax - m_threshold;

    float limited = m_threshold +
                    excess / (1.0f + excess * 8.0f);

    if (limited > m_ceiling) {
        limited = m_ceiling;
        m_clipCount.fetch_add(1, std::memory_order_relaxed);
    }
    return std::clamp(sign * limited, -1.0f, 1.0f);
}

void SafetyLimiter::process(float* L, float* R, int frames) {
    if (m_bypass) return;

    // Transparencia absoluta: si el bloque completo está debajo
    // del threshold y no existe reducción activa, no tocar la señal.
    // Evita degradación por lookahead/release en material limpio.
    float inputPeak = 0.0f;

    for (int i = 0; i < frames; ++i) {
        if (std::isfinite(L[i]))
            inputPeak = std::max(inputPeak, std::fabs(L[i]));
        if (std::isfinite(R[i]))
            inputPeak = std::max(inputPeak, std::fabs(R[i]));
    }

    if (inputPeak <= m_threshold && m_gainNow >= 0.999f) {
        m_peakBefore.store(inputPeak, std::memory_order_relaxed);
        m_gainReduction.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // Lazy-init del lookahead si nunca se llamó setSampleRate().
    if (m_delayLen == 0) setSampleRate(m_sampleRate);

    float peak  = 0.0f;
    int   clips = 0;
    const float ceil_ = m_ceiling;

    // ── Detección de peak de entrada (vectorizada NEON si disponible) ───────
    // Se mide sobre la ENTRADA (antes del limiting) — es lo que
    // AdaptiveDecisionEngine usa para reaccionar vía getGainReduction().
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    {
        float32x4_t vPeak = vdupq_n_f32(0.0f);
        int i = 0;
        for (; i + 4 <= frames; i += 4) {
            float32x4_t al = vabsq_f32(vld1q_f32(L + i));
            float32x4_t ar = vabsq_f32(vld1q_f32(R + i));
            vPeak = vmaxq_f32(vPeak, vmaxq_f32(al, ar));
        }
        float pk[4]; vst1q_f32(pk, vPeak);
        for (int j = 0; j < 4; ++j) peak = std::max(peak, pk[j]);
        for (; i < frames; ++i)
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
    }
#else
    for (int i = 0; i < frames; ++i)
        peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
#endif

    // ── Envolvente de ganancia por muestra con lookahead + release ──────────
    //
    // Clave del diseño (evita el overshoot que tenía la versión por bloque):
    // la ganancia necesaria se calcula sobre la muestra RETRASADA que está a
    // punto de salir — así el pico recibe exactamente su ganancia objetivo
    // cuando cruza la salida (ataque instantáneo en la línea de tiempo
    // retrasada), y la release 50 ms solo actúa entre picos, nunca durante.
    // El lookahead de 5 ms hace que la bajada de ganancia caiga sobre el
    // contenido anterior al pico (menor nivel), enmascarando el escalón.
    //
    // Detección de clipping: se cuenta en la ENTRADA (muestra que supera el
    // ceiling antes de ser rescatada). Cada evento cuenta exactamente una vez
    // — la seguridad dura de salida no vuelve a contar (fix del doble
    // clip-count, ver tests/test_regression_tuning.cpp Parche 6A).
    float gain = m_gainNow;

    for (int i = 0; i < frames; ++i) {
        const float inL = L[i];
        const float inR = R[i];

        // Clip detection (entrada, una sola vez por evento)
        if (std::fabs(inL) > ceil_ || std::fabs(inR) > ceil_) ++clips;

        // Envolvente: ataque instantáneo si hay que bajar, release exponencial
        const float need = std::min(computeGainForPeak(std::fabs(inL)),
                                    computeGainForPeak(std::fabs(inR)));
        if (need < gain) {
            gain = need;
        } else {
            gain = m_releaseCoef * gain + (1.0f - m_releaseCoef) * 1.0f;
        }

        // Lookahead: la ganancia se aplica a la muestra retrasada.
        const int rd = m_delayWrite;
        float outL = m_delayL[rd] * gain;
        float outR = m_delayR[rd] * gain;
        m_delayL[rd] = inL;
        m_delayR[rd] = inR;
        m_delayWrite = (rd + 1) % m_delayLen;

        // Seguridad dura final (NaN/Inf o residuo numérico) — silenciosa,
        // no re-cuenta clips (ya contados en la entrada).
        if (!std::isfinite(outL)) outL = 0.f;
        if (!std::isfinite(outR)) outR = 0.f;
        if (std::fabs(outL) > ceil_) outL = std::copysign(ceil_, outL);
        if (std::fabs(outR) > ceil_) outR = std::copysign(ceil_, outR);

        L[i] = std::clamp(outL, -1.0f, 1.0f);
        R[i] = std::clamp(outR, -1.0f, 1.0f);
    }

    m_gainNow = gain;

    if (clips > 0) m_clipCount.fetch_add(clips, std::memory_order_relaxed);
    m_peakBefore.store(peak, std::memory_order_relaxed);

    // Ganancia de reducción en dB (convención ya usada por
    // AdaptiveDecisionEngine::computeTargetGain — 20*log10(peak/ceiling)).
    float reduction_db = 0.0f;
    if (peak > ceil_ && peak > 1e-9f && ceil_ > 1e-9f) {
        reduction_db = 20.0f * std::log10(peak / ceil_);
        if (reduction_db < 0.0f) reduction_db = 0.0f;
    }
    m_gainReduction.store(reduction_db, std::memory_order_relaxed);
}

void SafetyLimiter::reset() {
    m_peakBefore.store(0.0f, std::memory_order_relaxed);
    m_gainReduction.store(0.0f, std::memory_order_relaxed);
    m_gainNow = 1.0f;
    if (!m_delayL.empty()) std::fill(m_delayL.begin(), m_delayL.end(), 0.f);
    if (!m_delayR.empty()) std::fill(m_delayR.begin(), m_delayR.end(), 0.f);
    m_delayWrite = 0;
    resetClipCount();
}

void SafetyLimiter::bypass(bool enabled) {
    m_bypass = enabled;
}

float SafetyLimiter::getPeakBeforeLimit() const {
    return m_peakBefore.load(std::memory_order_relaxed);
}

float SafetyLimiter::getGainReduction() const {
    return m_gainReduction.load(std::memory_order_relaxed);
}

int SafetyLimiter::getClipCount() const {
    return m_clipCount.load(std::memory_order_relaxed);
}

void SafetyLimiter::resetClipCount() {
    m_clipCount.store(0, std::memory_order_relaxed);
}

} // namespace ivanna
