#pragma once

#include <atomic>
#include <cmath>

namespace ivanna {

class SafetyLimiter {
public:
    SafetyLimiter() = default;

    void setParams(float threshold = 0.98855f, float ceiling = 0.989f);

    void process(float* L, float* R, int frames);

    void reset();

    void bypass(bool enabled);

    float getPeakBeforeLimit() const;
    float getGainReduction() const;

private:
    float limitSample(float x);

    float m_threshold = 0.98855f;
    float m_ceiling = 0.989f;
    bool m_bypass = false;

    std::atomic<float> m_peakBefore{0.0f};
    std::atomic<float> m_gainReduction{0.0f};
};

}
