#include "include/SaFStimulusRenderer.hpp"

#include <cmath>
#include <algorithm>

namespace ivanna {

SaFStimulusRenderer::SaFStimulusRenderer()
    :
    m_sampleRate(48000),
    m_blockSize(512),
    m_azimuth(0.0f),
    m_elevation(0.0f),
    m_ready(false)
{
}


bool SaFStimulusRenderer::initialize(
        uint32_t sampleRate,
        uint32_t blockSize)
{
    m_sampleRate = sampleRate;
    m_blockSize = blockSize;

    m_convolver.init(sampleRate);

    m_ready = true;

    return true;
}


void SaFStimulusRenderer::setDirection(
        float azimuth,
        float elevation)
{
    m_azimuth = azimuth;
    m_elevation = elevation;

    if(!m_ready)
        initialize(48000,512);

    m_convolver.set_position(
        azimuth,
        0.75f
    );
}


void SaFStimulusRenderer::createBiologicalStimulus(
        std::vector<float>& mono,
        float seconds)
{
    uint32_t samples =
        static_cast<uint32_t>(
            m_sampleRate * seconds
        );

    mono.resize(samples);


    constexpr float freq = 880.0f;


    for(uint32_t i=0;i<samples;i++)
    {
        float t =
            static_cast<float>(i) /
            static_cast<float>(m_sampleRate);


        float envelope =
            std::sin(
                3.14159265f *
                std::min(
                    t / 0.05f,
                    1.0f
                )
            );


        mono[i] =
            std::sin(
                2.0f *
                3.14159265f *
                freq *
                t
            )
            *
            envelope
            *
            0.25f;
    }
}



bool SaFStimulusRenderer::generateCalibrationStimulus(
        std::vector<float>& left,
        std::vector<float>& right,
        float durationSeconds)
{
    if(!m_ready)
        initialize(48000,512);


    std::vector<float> mono;


    createBiologicalStimulus(
        mono,
        durationSeconds
    );


    left.resize(mono.size());
    right.resize(mono.size());


    float az =
        m_azimuth *
        3.14159265f /
        180.0f;


    float leftGain =
        0.5f *
        (1.0f - std::sin(az));


    float rightGain =
        0.5f *
        (1.0f + std::sin(az));


    for(size_t i=0;i<mono.size();i++)
    {
        left[i] =
            mono[i] *
            leftGain;

        right[i] =
            mono[i] *
            rightGain;
    }


    return true;
}



void SaFStimulusRenderer::reset()
{
    m_azimuth = 0.0f;
    m_elevation = 0.0f;

    m_ready = false;
}


}
