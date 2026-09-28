#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Ivanna {

struct Vector3D {
    float x; // -1.0 to +1.0
    float y; // -1.0 to +1.0
    float z; // -1.0 to +1.0
};

struct Quaternion {
    float w{1.0f}, x{0.0f}, y{0.0f}, z{0.0f};
};

class BinauralRenderer {
public:
    static constexpr size_t HRTF_TAPS = 128;

    BinauralRenderer();
    void setOrientation(const Quaternion& quat);
    void processBinaural(const float* inMono, float* outLeft, float* outRight, size_t numFrames, Vector3D position);

private:

    float m_hrtfLeft[HRTF_TAPS];
    float m_hrtfRight[HRTF_TAPS];
    float m_history[HRTF_TAPS];
    size_t m_histIdx{0};
    Quaternion m_orientation;
};

class RoomSimulator {
public:
    RoomSimulator();
    void setParameters(float roomSize, float absorption, float dampening);
    void processReverb(const float* inL, const float* inR, float* outL, float* outR, size_t numFrames);

private:
    float m_roomSize{0.5f};
    float m_absorption{0.3f};
    float m_dampening{0.4f};

    // Feedback Delay Lines
    std::vector<float> m_delayBufferL;
    std::vector<float> m_delayBufferR;
    size_t m_delayIdxL{0};
    size_t m_delayIdxR{0};
};

} // namespace Ivanna
