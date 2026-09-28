#pragma once

// FIX (build CI roto — error real de namespace/firma): este header declaraba
// `Ivanna::SaFStimulusRenderer` como singleton con ctor privado y API
// instance()/renderDirection()/stop(), pero la implementación real
// (app/src/main/cpp/SaFStimulusRenderer.cpp) y su único consumidor JNI
// (jni/saf_stimulus_jni.cpp) usan `ivanna::SaFStimulusRenderer` como clase
// de instancia con initialize()/setDirection()/generateCalibrationStimulus().
// Resultado: 100 errores de compilación en CI ("namespace 'ivanna' does not
// enclose namespace 'SaFStimulusRenderer'", "calling a private constructor",
// etc.). Se verificó por grep que NADIE en el repo usa la API singleton
// vieja — la fuente de verdad es el .cpp + JNI, y este header se alinea a
// ellos.

#include <cstdint>
#include <vector>

#include "../spatial/hrtf_convolver.hpp"

namespace ivanna {

class SaFStimulusRenderer {
public:
    SaFStimulusRenderer();

    bool initialize(uint32_t sampleRate, uint32_t blockSize);

    void setDirection(float azimuth, float elevation);

    void createBiologicalStimulus(std::vector<float>& mono, float seconds);

    bool generateCalibrationStimulus(std::vector<float>& left,
                                     std::vector<float>& right,
                                     float durationSeconds);

    void reset();

private:
    uint32_t      m_sampleRate;
    uint32_t      m_blockSize;
    float         m_azimuth;
    float         m_elevation;
    bool          m_ready;
    HRTFConvolver m_convolver;
};

} // namespace ivanna
