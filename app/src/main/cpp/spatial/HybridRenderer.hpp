#pragma once

#include "HRTFInterpolator.hpp"
#include "RoomSimulator.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Ivanna {

struct NativeAudioObject {
    int id;
    float posX, posY, posZ;
    float gain;
};

class HybridRenderer {
public:
    HybridRenderer() {
        std::memset(m_delayHistoryL, 0, sizeof(m_delayHistoryL));
        std::memset(m_delayHistoryR, 0, sizeof(m_delayHistoryR));
    }
    ~HybridRenderer() = default;

    void updateObjects(const NativeAudioObject* objects, size_t count) {
        if (!objects || count == 0) {
            m_activeObjects.clear();
            return;
        }
        m_activeObjects.assign(objects, objects + count);
    }

    void setRoomConfig(const RoomConfig& config) noexcept {
        m_roomSimulator.setConfig(config);
    }

    void renderBinaural(const float* inStereo, float* outStereo, size_t frameCount) noexcept {
        if (!inStereo || !outStereo || frameCount == 0) return;
        const size_t n = std::min(frameCount, MAX_FRAMES);
        float tmpL[MAX_FRAMES]{};
        float tmpR[MAX_FRAMES]{};
        for (size_t i = 0; i < n; ++i) {
            tmpL[i] = inStereo[2 * i];
            tmpR[i] = inStereo[2 * i + 1];
        }
        m_roomSimulator.processStereo(tmpL, tmpR, tmpL, tmpR, n);
        for (size_t i = 0; i < n; ++i) {
            outStereo[2 * i] = tmpL[i];
            outStereo[2 * i + 1] = tmpR[i];
        }
    }

private:
    HRTFInterpolator m_hrtfInterpolator;
    RoomSimulator m_roomSimulator;
    std::vector<NativeAudioObject> m_activeObjects;

    static constexpr size_t MAX_FRAMES = 1024;
    float m_delayHistoryL[MAX_FRAMES + HRTF_TAPS];
    float m_delayHistoryR[MAX_FRAMES + HRTF_TAPS];
};

} // namespace Ivanna
