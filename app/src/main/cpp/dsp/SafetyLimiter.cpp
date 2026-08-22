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
    constexpr float kReleaseMs   = 50.0f;   // release suave
    constexpr float kKneeRatio   = 0.1f;    // soft-knee 10:1 sobre threshold
} // namespace

void SafetyLimiter::setParams(float threshold, float ceiling) {
    m_threshold = threshold;
    m_ceiling   = ceiling;
}

void SafetyLimiter::setSampleRate(float sampleRate) {
    if (sampleRate > 8000.f) m_sampleRate = sampleRate;
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
    if (m_bypass || frames <= 0) return;

    // Lazy-init del coeficiente de release si nunca se llamo setSampleRate().
    if (m_releaseCoef <= 0.f) setSampleRate(m_sampleRate);

    // ── Peak del bloque (vectorizado NEON si esta disponible) ───────────────
    // El bloque ES el lookahead: la ganancia se decide con el peak de TODO el
    // bloque antes de escribir una sola muestra, asi que ningun pico sale sin
    // su reduccion aplicada — sin delay line y sin latencia añadida.
    float peak  = 0.0f;
    int   clips = 0;
    const float ceil_ = m_ceiling;
    bool nonFinite = false;

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
        if (!std::isfinite(peak)) { nonFinite = true; peak = 0.f; }
    }
#else
    for (int i = 0; i < frames; ++i) {
        const float al = std::fabs(L[i]);
        const float ar = std::fabs(R[i]);
        if (!std::isfinite(al) || !std::isfinite(ar)) { nonFinite = true; continue; }
        peak = std::max(peak, std::max(al, ar));
    }
#endif

    // Recuento de clips sobre la ENTRADA: exactamente un evento por muestra
    // que supera el ceiling (convencion del Parche 6A, ver
    // tests/test_regression_tuning.cpp — la seguridad dura de salida no
    // vuelve a contar).
    if (peak > ceil_ || nonFinite) {
        for (int i = 0; i < frames; ++i) {
            const float al = std::fabs(L[i]);
            const float ar = std::fabs(R[i]);
            if (!std::isfinite(al) || !std::isfinite(ar)) { ++clips; continue; }
            if (al > ceil_ || ar > ceil_) ++clips;
        }
    }

    // Transparencia absoluta: material limpio sin reduccion residual sale
    // bit-exacto — y con la MISMA latencia (cero) que cuando el limiter actua.
    if (peak <= m_threshold && m_gainNow >= 0.99999f && !nonFinite) {
        m_gainNow = 1.0f;
        m_peakBefore.store(peak, std::memory_order_relaxed);
        m_gainReduction.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // Ganancia del bloque: ataque inmediato (el pico ya es conocido de
    // antemano), release exponencial de 50 ms entre picos. La ganancia NO
    // sigue la forma de onda muestra a muestra -> sin modulacion de amplitud
    // (que es distorsion armonica) ni pumping.
    const float blockGain = computeGainForPeak(peak);
    float gain = m_gainNow;
    if (blockGain < gain) gain = blockGain;

    for (int i = 0; i < frames; ++i) {
        if (blockGain >= gain) {
            gain = m_releaseCoef * gain + (1.0f - m_releaseCoef);
            if (gain > 1.0f) gain = 1.0f;
        }

        float outL = L[i] * gain;
        float outR = R[i] * gain;

        // Seguridad dura final (NaN/Inf o residuo numerico) — silenciosa.
        if (!std::isfinite(outL)) outL = 0.f;
        if (!std::isfinite(outR)) outR = 0.f;
        if (std::fabs(outL) > ceil_) outL = std::copysign(ceil_, outL);
        if (std::fabs(outR) > ceil_) outR = std::copysign(ceil_, outR);

        L[i] = outL;
        R[i] = outR;
    }

    m_gainNow = gain;

    if (clips > 0) m_clipCount.fetch_add(clips, std::memory_order_relaxed);
    m_peakBefore.store(peak, std::memory_order_relaxed);

    // Reduccion en dB (convencion de AdaptiveDecisionEngine::computeTargetGain).
    float reduction_db = 0.0f;
    if (peak > ceil_ && peak > 1e-9f && ceil_ > 1e-9f)
        reduction_db = 20.0f * std::log10(peak / ceil_);
    if (reduction_db < 0.0f) reduction_db = 0.0f;
    m_gainReduction.store(reduction_db, std::memory_order_relaxed);
}

void SafetyLimiter::reset() {
    m_peakBefore.store(0.0f, std::memory_order_relaxed);
    m_gainReduction.store(0.0f, std::memory_order_relaxed);
    m_gainNow = 1.0f;
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
