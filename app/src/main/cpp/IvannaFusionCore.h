#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "IvannaFusionCore.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// FIX (CI rojo — "namespace 'Ivanna' does not enclose namespace
// 'IvannaFusionEngine'"): la clase se declaraba en ámbito GLOBAL mientras
// IvannaFusionCore.cpp la define dentro de `namespace Ivanna {}`. Todas las
// clases colaboradoras (HrtfManager, EvolutionaryEQ, Psychoacoustics,
// IvannaAudioClassifier, IvannaVoiceProsodyEngine, IvannaSuperAgentMemory)
// también viven en namespace Ivanna — la clase se declara ahora en el mismo
// namespace, alineada con sus definiciones.
//
// Además se restaura la API de compatibilidad que omega_effect.cpp invoca
// (ctor con sampleRate, initSpatial, processStereo, loadCustomHrtf, setters
// del snapshot del Control Bus y getters de subsistemas), siguiendo el mismo
// contrato que documenta cpp_snapshot/IvannaFusionCore.hpp.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ivanna {

class HrtfManager;
class EvolutionaryEQ;
class Psychoacoustics;
class IvannaAudioClassifier;
class IvannaVoiceProsodyEngine;
class IvannaSuperAgentMemory;

class IvannaFusionEngine {
public:

    IvannaFusionEngine();

    ~IvannaFusionEngine();

    // Constructor de compatibilidad: omega_effect.cpp crea la instancia con el
    // sampleRate capturado de AudioFlinger (ej. 48000 Hz). Se acepta sin
    // almacenarlo; la tasa de muestreo real la gestionan los subsistemas.
    explicit IvannaFusionEngine(float /*sampleRate*/) : IvannaFusionEngine() {}

    void process(Ivanna::AudioBuffer* buffer);

    void runAcousticProfiling();

    void setGoldenEarMode(bool enable);

    void applyGoldenEarGAN(Ivanna::AudioBuffer* buffer);

    void setSafLatentParams(const float q[7]) noexcept;

    // ── API de compatibilidad con omega_effect.cpp / OmegaControlBus ─────────
    // Estos métodos reciben los parámetros del Control Plane (snapshot SHM) y
    // los enrutan a los subsistemas internos del engine.

    // Inicialización espacial tras SET_CONFIG de AudioFlinger.
    void initSpatial(float /*sr*/, int /*blockSize*/) noexcept {
        runAcousticProfiling();
    }

    // Procesa N frames estéreo desinterleaved L/R en chunks de BLOCK_SIZE.
    // Llamado desde omega_process() en la ruta caliente de AudioFlinger.
    void processStereo(float* left, float* right, size_t frames) noexcept {
        size_t offset = 0;
        while (offset < frames) {
            size_t chunk = frames - offset;
            if (chunk > Ivanna::BLOCK_SIZE) chunk = Ivanna::BLOCK_SIZE;
            Ivanna::AudioBuffer buf{};
            std::memcpy(buf.left,  left  + offset, chunk * sizeof(float));
            std::memcpy(buf.right, right + offset, chunk * sizeof(float));
            process(&buf);
            std::memcpy(left  + offset, buf.left,  chunk * sizeof(float));
            std::memcpy(right + offset, buf.right, chunk * sizeof(float));
            offset += chunk;
        }
    }

    // Carga un dataset HRTF medido (formato IHR1) desde disco.
    // Devuelve false si el archivo no existe o la cabecera es inválida.
    bool loadCustomHrtf(const char* path) noexcept;

    // Actualiza la pose de cabeza para el render binaural.
    void updateHeadPose(float yaw, float pitch, float roll) noexcept;

    // Parámetros del snapshot OmegaDspSnapshot → subsistemas internos.
    // Stubs deliberados: permiten compilar el puente OmegaControlBus mientras
    // se cablea hacia StereoWidener / HarmonicExciter / compresor.
    void setSpatialWidth(float /*width*/) noexcept {}
    void setHarmonicGain(float /*gain*/) noexcept {}
    void setCompressorParams(float /*thresholdDb*/, float /*ratio*/) noexcept {}
    void setRouteProfile(float /*bassDb*/, float /*dialogDb*/,
                         float /*widener*/) noexcept {}
    void setEqGains(const float* /*gains*/, int /*n*/,
                    float /*listenPhon*/, float /*refPhon*/) noexcept {}
    void setIntensity(float /*intensity*/) noexcept {}

    IvannaAudioClassifier*     getClassifier()    const noexcept { return m_classifier; }
    IvannaVoiceProsodyEngine*  getProsodyEngine() const noexcept { return m_prosody; }

private:

    float sampleRate_ = 48000.0f;

    bool goldenEarMode_ = false;

    float safLatent_[7]{};


    HrtfManager* m_hrtf = nullptr;
    EvolutionaryEQ* m_evoEq = nullptr;
    Psychoacoustics* m_psycho = nullptr;
    IvannaAudioClassifier* m_classifier = nullptr;
    IvannaVoiceProsodyEngine* m_prosody = nullptr;
    IvannaSuperAgentMemory* m_memory = nullptr;

    bool m_goldenEarActive = false;

    struct FilterState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    FilterState m_chebLpfL;
    FilterState m_chebLpfR;

};

} // namespace Ivanna

// ── Alias de compatibilidad — NAMESPACE GLOBAL ───────────────────────────────
// CRÍTICO: el alias debe estar en el namespace GLOBAL, no en Ivanna{}.
// omega_effect.cpp referencia el nombre histórico IvannaFusionCore sin
// cualificar (además de `using namespace Ivanna;`); sin este alias global el
// tipo no se resuelve en los TUs que solo incluyen este header.
using IvannaFusionCore = Ivanna::IvannaFusionEngine;
