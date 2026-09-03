#pragma once

#include <cstdint>
#include "IvannaFusionCore.hpp"

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

    void process(Ivanna::AudioBuffer* buffer);

    void runAcousticProfiling();

    void setGoldenEarMode(bool enable);

    void applyGoldenEarGAN(Ivanna::AudioBuffer* buffer);

    void setSafLatentParams(const float q[7]) noexcept;


private:

    float sampleRate_;

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
