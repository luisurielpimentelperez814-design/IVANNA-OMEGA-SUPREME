#pragma once

#include <cstddef>
#include <vector>

namespace Ivanna {

struct RoomConfig {
    float roomWidthMeters = 5.0f;
    float roomLengthMeters = 7.0f;
    float roomHeightMeters = 3.0f;
    float absorptionFactor = 0.35f;
    float wetMix = 0.25f;
};

class RoomSimulator {
public:
    RoomSimulator();
    ~RoomSimulator() = default;

    void setConfig(const RoomConfig& config);
    void processStereo(const float* inL, const float* inR,
                        float* outL, float* outR,
                        size_t numSamples);

private:
    RoomConfig m_config;
    std::vector<float> m_combBufferL1, m_combBufferL2;
    std::vector<float> m_combBufferR1, m_combBufferR2;
    size_t m_combPosL1{0}, m_combPosL2{0};
    size_t m_combPosR1{0}, m_combPosR2{0};
};

} // namespace Ivanna
