#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "RoomSimulator.hpp"

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

    BinauralRenderer() noexcept {
        std::memset(m_hrtfLeft, 0, sizeof(m_hrtfLeft));
        std::memset(m_hrtfRight, 0, sizeof(m_hrtfRight));
        std::memset(m_history, 0, sizeof(m_history));
        m_hrtfLeft[0] = 1.0f;
        m_hrtfRight[0] = 1.0f;
    }

    void setOrientation(const Quaternion& quat) noexcept {
        m_orientation = quat;
    }

    void processBinaural(const float* inMono, float* outLeft, float* outRight,
                         size_t numFrames, Vector3D position) noexcept {
        if (!inMono || !outLeft || !outRight) return;
        const float pan = std::clamp(position.x, -1.0f, 1.0f);
        const float gL = std::sqrt(0.5f * (1.0f - pan));
        const float gR = std::sqrt(0.5f * (1.0f + pan));
        for (size_t i = 0; i < numFrames; ++i) {
            m_history[m_histIdx] = inMono[i];
            m_histIdx = (m_histIdx + 1) % HRTF_TAPS;
            outLeft[i] = inMono[i] * gL * m_hrtfLeft[0];
            outRight[i] = inMono[i] * gR * m_hrtfRight[0];
        }
    }

private:
    float m_hrtfLeft[HRTF_TAPS];
    float m_hrtfRight[HRTF_TAPS];
    float m_history[HRTF_TAPS];
    size_t m_histIdx{0};
    Quaternion m_orientation;
};

} // namespace Ivanna
