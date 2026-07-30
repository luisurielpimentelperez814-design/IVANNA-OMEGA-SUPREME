#include "HybridRenderer.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace Ivanna {

HybridRenderer::HybridRenderer() {
    std::memset(m_delayHistoryL, 0, sizeof(m_delayHistoryL));
    std::memset(m_delayHistoryR, 0, sizeof(m_delayHistoryR));
}

void HybridRenderer::updateObjects(const NativeAudioObject* objects, size_t count) {
    m_activeObjects.assign(objects, objects + count);
}

void HybridRenderer::setRoomConfig(const RoomConfig& config) {
    m_roomSimulator.setConfig(config);
}

void HybridRenderer::renderBinaural(const float* inStereo, float* outStereo, size_t frameCount) {
    size_t frames = std::min(frameCount, MAX_FRAMES);

    float tempL[MAX_FRAMES];
    float tempR[MAX_FRAMES];
    for (size_t f = 0; f < frames; ++f) {
        tempL[f] = inStereo[f * 2];
        tempR[f] = inStereo[f * 2 + 1];
    }

    std::memmove(m_delayHistoryL, m_delayHistoryL + frames, HRTF_TAPS * sizeof(float));
    std::memmove(m_delayHistoryR, m_delayHistoryR + frames, HRTF_TAPS * sizeof(float));
    std::memcpy(m_delayHistoryL + HRTF_TAPS, tempL, frames * sizeof(float));
    std::memcpy(m_delayHistoryR + HRTF_TAPS, tempR, frames * sizeof(float));

    float binL[MAX_FRAMES] = {0.0f};
    float binR[MAX_FRAMES] = {0.0f};

    if (!m_activeObjects.empty()) {
        for (const auto& obj : m_activeObjects) {
            float azDeg = std::atan2(obj.posX, obj.posZ) * 180.0f / 3.14159265f;
            float elDeg = std::atan2(obj.posY, std::hypot(obj.posX, obj.posZ)) * 180.0f / 3.14159265f;

            float hrtfL[HRTF_TAPS];
            float hrtfR[HRTF_TAPS];
            m_hrtfInterpolator.getInterpolatedHRTF(azDeg, elDeg, hrtfL, hrtfR);

            for (size_t i = 0; i < frames; ++i) {
                float accL = 0.0f;
                float accR = 0.0f;

#if defined(__ARM_NEON) || defined(__aarch64__)
                float32x4_t vecAccL = vdupq_n_f32(0.0f);
                float32x4_t vecAccR = vdupq_n_f32(0.0f);

                for (size_t t = 0; t < HRTF_TAPS; t += 4) {
                    float32x4_t vHistL = vld1q_f32(&m_delayHistoryL[i + t]);
                    float32x4_t vHistR = vld1q_f32(&m_delayHistoryR[i + t]);
                    float32x4_t vHrtfL = vld1q_f32(&hrtfL[t]);
                    float32x4_t vHrtfR = vld1q_f32(&hrtfR[t]);

                    vecAccL = vmlaq_f32(vecAccL, vHistL, vHrtfL);
                    vecAccR = vmlaq_f32(vecAccR, vHistR, vHrtfR);
                }

                accL = vaddvq_f32(vecAccL);
                accR = vaddvq_f32(vecAccR);
#else
                for (size_t t = 0; t < HRTF_TAPS; ++t) {
                    accL += m_delayHistoryL[i + t] * hrtfL[t];
                    accR += m_delayHistoryR[i + t] * hrtfR[t];
                }
#endif
                binL[i] += accL * obj.gain;
                binR[i] += accR * obj.gain;
            }
        }
    } else {
        std::memcpy(binL, tempL, frames * sizeof(float));
        std::memcpy(binR, tempR, frames * sizeof(float));
    }

    m_roomSimulator.processStereo(binL, binR, tempL, tempR, frames);

    for (size_t f = 0; f < frames; ++f) {
        outStereo[f * 2]     = tempL[f];
        outStereo[f * 2 + 1] = tempR[f];
    }
}

} // namespace Ivanna
