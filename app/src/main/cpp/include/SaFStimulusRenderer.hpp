#pragma once

#include <atomic>
#include <vector>

namespace Ivanna {

class SaFStimulusRenderer {
public:

    static SaFStimulusRenderer& instance();

    bool initialize();

    bool renderDirection(
        float azimuth,
        float elevation,
        float seconds
    );

    void stop();

private:

    SaFStimulusRenderer() = default;

    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_running{false};

    std::vector<float> m_buffer;

    void generateBiometricStimulus(
        float azimuth,
        float elevation
    );
};

}
