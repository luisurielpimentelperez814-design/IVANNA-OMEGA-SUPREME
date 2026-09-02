#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class HrtfManager {
public:
    HrtfManager();
    ~HrtfManager() = default;

    void updateListenerPosition(float azimuth, float elevation);
    void processSpatialization(AudioBuffer* buffer);

private:
    float m_azimuth = 0.0f;
    float m_elevation = 0.0f;

    // Buffers de convolución HRTF en el dominio del tiempo
    float m_histL[BLOCK_SIZE * 2] = {0.0f};
    float m_histR[BLOCK_SIZE * 2] = {0.0f};

    float m_hrtfLL[BLOCK_SIZE] = {0.0f};
    float m_hrtfRL[BLOCK_SIZE] = {0.0f};
    float m_hrtfRR[BLOCK_SIZE] = {0.0f};
    float m_hrtfLR[BLOCK_SIZE] = {0.0f};
};

} // namespace Ivanna
