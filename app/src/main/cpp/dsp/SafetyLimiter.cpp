#include "../include/SafetyLimiter.h"
#include <algorithm>
#include <cmath>  // FIX: std::max — el NDK (libc++) no lo incluye
                       // transitivamente vía <cmath>, a diferencia de otros
                       // toolchains. Sin esto, ninja falla en arm64-v8a:
                       // "no member named 'max' in namespace 'std'".
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna {

void SafetyLimiter::setParams(float threshold, float ceiling) {
    m_threshold = threshold;
    m_ceiling = ceiling;
}

float SafetyLimiter::limitSample(float x) {

    if (!std::isfinite(x)) {
        m_clipCount.fetch_add(1, std::memory_order_relaxed);
        return 0.0f;
    }

    float ax = std::fabs(x);

    if (ax <= m_threshold)
        return x;

    float sign = x < 0.0f ? -1.0f : 1.0f;

    float excess = ax - m_threshold;

    float limited = m_threshold +
                    excess * 0.1f;

    if (limited > m_ceiling) {
        limited = m_ceiling;
        m_clipCount.fetch_add(1, std::memory_order_relaxed);
    }

    // FIX: eliminado segundo fetch_add redundante.
    // ceiling=0.989 < 1.0, así que todo clip que activa (limited>ceiling)
    // también activaría el chequeo siguiente — contaría 2x el mismo evento.
    return std::clamp(sign * limited, -1.0f, 1.0f);
}


void SafetyLimiter::process(float* L, float* R, int frames) {

    if (m_bypass)
        return;

    const float thresh = m_threshold;
    const float ceil_  = m_ceiling;
    float peak  = 0.0f;
    int   clips = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // ── Vectorización NEON 4-wide branchless ─────────────────────────────
    // Soft-knee branchless: excess = max(0, |x| - thresh)
    //   limited = |x| + excess * (knee_ratio - 1)  = thresh + excess * 0.1
    //   out     = copysign(min(limited, ceil), x)
    //   out     = clamp(out, -1, 1)
    // Cero branches en el hot path — eliminado el if(ax <= thresh)/if(limited > ceiling)
    // que impedía la vectorización automática del compilador.
    const float32x4_t vThresh  = vdupq_n_f32(thresh);
    const float32x4_t vCeil    = vdupq_n_f32(ceil_);
    const float32x4_t vOne     = vdupq_n_f32(1.0f);
    const float32x4_t vNegOne  = vdupq_n_f32(-1.0f);
    const float32x4_t vZero    = vdupq_n_f32(0.0f);
    const float32x4_t vKnee    = vdupq_n_f32(-0.9f); // (knee_ratio - 1) = 0.1 - 1
    float32x4_t vPeak          = vdupq_n_f32(0.0f);
    uint32x4_t  vClips         = vdupq_n_u32(0);

    int i = 0;
    for (; i + 4 <= frames; i += 4) {
        float32x4_t lv = vld1q_f32(L + i);
        float32x4_t rv = vld1q_f32(R + i);

        float32x4_t al = vabsq_f32(lv);
        float32x4_t ar = vabsq_f32(rv);

        // Peak tracking
        vPeak = vmaxq_f32(vPeak, vmaxq_f32(al, ar));

        // excess = max(0, |x| - thresh)
        float32x4_t exL = vmaxq_f32(vZero, vsubq_f32(al, vThresh));
        float32x4_t exR = vmaxq_f32(vZero, vsubq_f32(ar, vThresh));

        // limited = |x| + excess * (-0.9)  →  if excess=0: limited=|x|; else soft-knee
        float32x4_t limL = vaddq_f32(al, vmulq_f32(exL, vKnee));
        float32x4_t limR = vaddq_f32(ar, vmulq_f32(exR, vKnee));

        // Clip count: samples where raw limited > ceil before clamping
        uint32x4_t clipL = vcgtq_f32(limL, vCeil);
        uint32x4_t clipR = vcgtq_f32(limR, vCeil);
        vClips = vaddq_u32(vClips, vandq_u32(clipL, vdupq_n_u32(1)));
        vClips = vaddq_u32(vClips, vandq_u32(clipR, vdupq_n_u32(1)));

        // Hard ceiling
        limL = vminq_f32(limL, vCeil);
        limR = vminq_f32(limR, vCeil);

        // Restore sign: if lv < 0 → -limL, else +limL
        lv = vbslq_f32(vcltq_f32(lv, vZero), vnegq_f32(limL), limL);
        rv = vbslq_f32(vcltq_f32(rv, vZero), vnegq_f32(limR), limR);

        // Clamp to [-1, 1]
        lv = vminq_f32(vOne, vmaxq_f32(vNegOne, lv));
        rv = vminq_f32(vOne, vmaxq_f32(vNegOne, rv));

        vst1q_f32(L + i, lv);
        vst1q_f32(R + i, rv);
    }

    // Reduce NEON accumulators
    {
        float pk[4]; vst1q_f32(pk, vPeak);
        for (int j = 0; j < 4; ++j) peak = std::max(peak, pk[j]);
        uint32_t cl[4]; vst1q_u32(cl, vClips);
        for (int j = 0; j < 4; ++j) clips += (int)cl[j];
    }

    // Scalar tail (frames % 4 remaining samples)
    for (; i < frames; ++i) {
        peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        L[i] = limitSample(L[i]);
        R[i] = limitSample(R[i]);
    }
#else
    // Scalar fallback (x86 / non-NEON)
    for (int i = 0; i < frames; ++i) {
        peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        L[i] = limitSample(L[i]);
        R[i] = limitSample(R[i]);
    }
#endif

    if (clips > 0)
        m_clipCount.fetch_add(clips, std::memory_order_relaxed);

    m_peakBefore.store(peak, std::memory_order_relaxed);

    // FIX: almacenar en dB (no en amplitud lineal).
    // AdaptiveDecisionEngine::computeTargetGain() interpreta gain_reduction_db
    // como dB — un valor lineal de 0.06 se leía como 0.06 dB (sin reacción)
    // cuando el real era ~0.5 dB. 20*log10(peak/ceiling) da el valor correcto.
    float reduction_db = 0.0f;
    if (peak > m_ceiling && peak > 1e-9f && m_ceiling > 1e-9f) {
        reduction_db = 20.0f * std::log10(peak / m_ceiling);
        if (reduction_db < 0.0f) reduction_db = 0.0f; // seguridad numérica
    }

    m_gainReduction.store(
        reduction_db,
        std::memory_order_relaxed
    );
}


void SafetyLimiter::reset() {

    m_peakBefore.store(
        0.0f,
        std::memory_order_relaxed
    );

    m_gainReduction.store(
        0.0f,
        std::memory_order_relaxed
    );
    resetClipCount();
}


void SafetyLimiter::bypass(bool enabled) {
    m_bypass = enabled;
}


float SafetyLimiter::getPeakBeforeLimit() const {
    return m_peakBefore.load(
        std::memory_order_relaxed
    );
}


float SafetyLimiter::getGainReduction() const {
    return m_gainReduction.load(
        std::memory_order_relaxed
    );
}


int SafetyLimiter::getClipCount() const {
    return m_clipCount.load(std::memory_order_relaxed);
}


void SafetyLimiter::resetClipCount() {
    m_clipCount.store(0, std::memory_order_relaxed);
}


}
