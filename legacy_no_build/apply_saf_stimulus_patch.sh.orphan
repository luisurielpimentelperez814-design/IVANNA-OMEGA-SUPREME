#!/system/bin/sh

set -e

echo "[SAF] Creando renderer de estímulo HRTF..."

mkdir -p app/src/main/cpp/include

cat <<'CPP' > app/src/main/cpp/include/SaFStimulusRenderer.hpp
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
CPP


cat <<'CPP' > app/src/main/cpp/SaFStimulusRenderer.cpp
#include "include/SaFStimulusRenderer.hpp"

#include <cmath>
#include <algorithm>

namespace Ivanna {


SaFStimulusRenderer&
SaFStimulusRenderer::instance()
{
    static SaFStimulusRenderer g;
    return g;
}


bool SaFStimulusRenderer::initialize()
{
    m_ready.store(true);
    return true;
}


void SaFStimulusRenderer::generateBiometricStimulus(
        float azimuth,
        float elevation)
{

    /*
       Estímulo biomimético inicial:

       - frecuencia portadora modulada
       - diferencia interaural simulada
       - envolvente tipo neurona auditiva
       - transición suave evitando clicks

       Posteriormente aquí entra:
       HRTFConvolver
       HRIR real
       SAF latent q[7]
    */


    constexpr float sr = 48000.0f;

    constexpr float duration = 0.35f;

    size_t samples =
        static_cast<size_t>(sr * duration);


    m_buffer.resize(samples * 2);


    float az =
        azimuth * 3.14159265f / 180.0f;


    float leftGain =
        0.5f *
        (1.0f - sinf(az));


    float rightGain =
        0.5f *
        (1.0f + sinf(az));


    for(size_t i=0;i<samples;i++)
    {

        float t =
            static_cast<float>(i)/sr;


        float envelope =
            sinf(
                3.14159265f *
                std::min(
                    t / 0.05f,
                    1.0f
                )
            );


        float carrier =
            sinf(
                2.0f *
                3.14159265f *
                880.0f *
                t
            );


        float neural =
            carrier *
            envelope *
            0.25f;


        m_buffer[i*2]
            =
            neural * leftGain;


        m_buffer[i*2+1]
            =
            neural * rightGain;

    }

}


bool SaFStimulusRenderer::renderDirection(
        float azimuth,
        float elevation,
        float seconds)
{

    if(!m_ready.load())
        initialize();


    m_running.store(true);


    generateBiometricStimulus(
        azimuth,
        elevation
    );


    /*
       Punto de conexión futuro:

       AudioTrack / AAudio
          |
          |
       HRTFConvolver
          |
          |
       SpatialRenderer
          |
          |
       dispositivo


    */


    return true;
}



void SaFStimulusRenderer::stop()
{
    m_running.store(false);
    m_buffer.clear();
}


}
CPP


echo "[SAF] Insertando en CMake..."

grep -q "SaFStimulusRenderer.cpp" app/src/main/cpp/CMakeLists.txt || {

python3 - <<'PY'
p="app/src/main/cpp/CMakeLists.txt"

s=open(p).read()

needle="SafHRTFDatasetBridge.cpp"

s=s.replace(
needle,
needle+"\n    SaFStimulusRenderer.cpp"
)

open(p,"w").write(s)
PY

}


echo "[SAF] Patch aplicado"

