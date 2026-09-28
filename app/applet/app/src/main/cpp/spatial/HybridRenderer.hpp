#pragma once

#include "HRTFInterpolator.hpp"
#include "RoomSimulator.hpp"
#include <cstddef>
#include <vector>

namespace Ivanna {

struct NativeAudioObject {
    int id;
    float posX, posY, posZ;
    float gain;
};

class HybridRenderer {
public:
    HybridRenderer();
    ~HybridRenderer() = default;

    void updateObjects(const NativeAudioObject* objects, size_t count);
    void setRoomConfig(const RoomConfig& config);

    void renderBinaural(const float* inStereo, float* outStereo, size_t frameCount);

private:
    HRTFInterpolator m_hrtfInterpolator;
    RoomSimulator m_roomSimulator;
    std::vector<NativeAudioObject> m_activeObjects;
    
    static constexpr size_t MAX_FRAMES = 1024;
    float m_delayHistoryL[MAX_FRAMES + HRTF_TAPS];
    float m_delayHistoryR[MAX_FRAMES + HRTF_TAPS];
};

} // namespace Ivanna
