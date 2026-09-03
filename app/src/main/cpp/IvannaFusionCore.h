#pragma once

#include "AudioBuffer.h"
#include <cstdint>

class IvannaFusionEngine {
public:

    explicit IvannaFusionEngine(float sampleRate);

    ~IvannaFusionEngine();

    void process(AudioBuffer* buffer);

    void runAcousticProfiling();

    void setGoldenEarMode(bool enable);

    void applyGoldenEarGAN(AudioBuffer* buffer);

    void setSafLatentParams(const float q[7]) noexcept;


private:

    float sampleRate_;

    bool goldenEarMode_ = false;

    float safLatent_[7]{};

};
