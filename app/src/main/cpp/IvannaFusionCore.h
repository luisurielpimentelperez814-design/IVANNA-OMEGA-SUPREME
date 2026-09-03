#pragma once

#include <cstdint>
#include "IvannaFusionCore.hpp"

class IvannaFusionEngine {
public:

    explicit IvannaFusionEngine(float sampleRate);

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

};
