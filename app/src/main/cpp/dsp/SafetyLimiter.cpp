#include "../include/SafetyLimiter.h"
#include <algorithm>  // FIX: std::max — el NDK (libc++) no lo incluye
                       // transitivamente vía <cmath>, a diferencia de otros
                       // toolchains. Sin esto, ninja falla en arm64-v8a:
                       // "no member named 'max' in namespace 'std'".

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

    const float clipped = std::clamp(sign * limited, -1.0f, 1.0f);
    if (std::fabs(clipped) >= 1.0f && ax > 1.0f) {
        m_clipCount.fetch_add(1, std::memory_order_relaxed);
    }
    return clipped;
}


void SafetyLimiter::process(float* L, float* R, int frames) {

    if (m_bypass)
        return;

    float peak = 0.0f;

    for (int i = 0; i < frames; i++) {

        float beforeL = L[i];
        float beforeR = R[i];

        peak = std::max(
            peak,
            std::max(std::fabs(beforeL),
                     std::fabs(beforeR))
        );

        L[i] = limitSample(beforeL);
        R[i] = limitSample(beforeR);
    }

    m_peakBefore.store(
        peak,
        std::memory_order_relaxed
    );

    float reduction =
        peak > m_ceiling ?
        peak - m_ceiling :
        0.0f;

    m_gainReduction.store(
        reduction,
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
