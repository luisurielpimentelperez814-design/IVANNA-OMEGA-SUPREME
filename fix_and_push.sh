#!/bin/bash
set -e

echo "=== [IVANNA-OMEGA-SUPREME] Aplicando correcciones de Kernel DSP & TinyML ==="

# 1. Update IvannaFusionCore.hpp
cat << 'CORE_EOF' > app/src/main/cpp/IvannaFusionCore.hpp
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 128;
constexpr size_t BANDS_512 = 512;
constexpr size_t FIR_TAPS = 256;
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float SAMPLING_RATE = 48000.0f;

struct ALIGN_NEON AudioBuffer {
    float left[BLOCK_SIZE];
    float right[BLOCK_SIZE];
};

class IvannaFusionCore {
public:
    IvannaFusionCore() = default;
    virtual ~IvannaFusionCore() = default;

    virtual void processBlock(AudioBuffer* buffer) { (void)buffer; }
    virtual void setParameter(uint32_t paramId, float value) { (void)paramId; (void)value; }
};

class HrtfManager;
class Psychoacoustics;
class IvannaAudioClassifier;

class IvannaFusionEngine : public IvannaFusionCore {
public:
    IvannaFusionEngine();
    virtual ~IvannaFusionEngine();

    void runAcousticProfiling();
    void process(AudioBuffer* buffer);

    IvannaAudioClassifier* getClassifier() const noexcept { return m_classifier; }

    void processBlock(AudioBuffer* buffer) override { process(buffer); }
    void setParameter(uint32_t paramId, float value) override { (void)paramId; (void)value; }

private:
    bool m_goldenEarActive = false;
    HrtfManager* m_hrtf = nullptr;
    Psychoacoustics* m_psycho = nullptr;
    IvannaAudioClassifier* m_classifier = nullptr;
};

} // namespace Ivanna
CORE_EOF

# 2. Update EvolutionaryEQ.hpp
cat << 'EQ_EOF' > app/src/main/cpp/EvolutionaryEQ.hpp
#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ : public IvannaFusionCore {
public:
    EvolutionaryEQ();
    void calibrateTargetRoom();
    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES(); // Evolution step

    void processBlock(AudioBuffer* buffer) override { processNEON(buffer); }
    void setParameter(uint32_t paramId, float value) override { (void)paramId; (void)value; }

private:
    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];
    float m_fitnessScore = 0.0f;
};

} // namespace Ivanna
EQ_EOF

# 3. Update HrtfManager.hpp
cat << 'HRTF_H_EOF' > app/src/main/cpp/HrtfManager.hpp
#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class HrtfManager {
public:
    HrtfManager();
    void processSpatialHrtf(AudioBuffer* buffer, float azimuth, float elevation);

private:
    ALIGN_NEON float m_hrtfLL[FIR_TAPS];
    ALIGN_NEON float m_hrtfRL[FIR_TAPS];
    ALIGN_NEON float m_hrtfRR[FIR_TAPS];
    ALIGN_NEON float m_hrtfLR[FIR_TAPS];
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];
};

} // namespace Ivanna
HRTF_H_EOF

# 4. Update HrtfManager.cpp
cat << 'HRTF_CPP_EOF' > app/src/main/cpp/HrtfManager.cpp
#include "HrtfManager.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

HrtfManager::HrtfManager() {
    std::fill(std::begin(m_histL), std::end(m_histL), 0.0f);
    std::fill(std::begin(m_histR), std::end(m_histR), 0.0f);
    std::fill(std::begin(m_hrtfLL), std::end(m_hrtfLL), 0.0f);
    std::fill(std::begin(m_hrtfRL), std::end(m_hrtfRL), 0.0f);
    std::fill(std::begin(m_hrtfRR), std::end(m_hrtfRR), 0.0f);
    std::fill(std::begin(m_hrtfLR), std::end(m_hrtfLR), 0.0f);

    m_hrtfLL[0] = 1.0f;
    m_hrtfRR[0] = 1.0f;
}

void HrtfManager::processSpatialHrtf(AudioBuffer* buffer, float azimuth, float elevation) {
    (void)elevation;
    float rad = azimuth * (3.14159265f / 180.0f);
    float panL = std::cos(0.5f * (rad + 1.57079632f));
    float panR = std::sin(0.5f * (rad + 1.57079632f));

    m_hrtfLL[0] = panL;
    m_hrtfRR[0] = panR;
    m_hrtfRL[0] = panR * 0.15f;
    m_hrtfLR[0] = panL * 0.15f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float outL = 0.0f;
        float outR = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            float xL = m_histL[i + t];
            float xR = m_histR[i + t];

            outL += xL * m_hrtfLL[t] + xR * m_hrtfRL[t];
            outR += xR * m_hrtfRR[t] + xL * m_hrtfLR[t];
        }

        buffer->left[i] = outL;
        buffer->right[i] = outR;
    }

    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
HRTF_CPP_EOF

# 5. Update HarmonicExciter.cpp
cat << 'EXCITER_CPP_EOF' > app/src/main/cpp/dsp/HarmonicExciter.cpp
#include "../include/HarmonicExciter.h"
#include <cmath>
#include <algorithm>

namespace Ivanna {

HarmonicExciter::HarmonicExciter() {
    DSPParams defaultParams;
    setParams(defaultParams);
}

void HarmonicExciter::setParams(const DSPParams& params) {
    drive_ = params.exciterDrive;
    mix_ = params.exciterMix;
    harmonicsMix_ = params.exciterHarmonicsMix;

    float fc = 2400.0f / 48000.0f;
    float q = 0.7071f;
    hpfL_.setHighpass(fc, q);
    hpfR_.setHighpass(fc, q);
}

void HarmonicExciter::process(float* left, float* right, size_t numSamples) {
    if (mix_ <= 0.001f) return;

    for (size_t i = 0; i < numSamples; ++i) {
        float inL = left[i];
        float inR = right[i];

        float hpL = hpfL_.process(inL);
        float hpR = hpfR_.process(inR);

        float drivenL = hpL * (1.0f + drive_ * 3.0f);
        float drivenR = hpR * (1.0f + drive_ * 3.0f);

        auto exciteSample = [this](float x) {
            float x2 = x * x;
            float even = x2 * 0.5f;
            float odd = (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
            return (1.0f - harmonicsMix_) * odd + harmonicsMix_ * even;
        };

        float excL = exciteSample(drivenL);
        float excR = exciteSample(drivenR);

        left[i] = inL + excL * mix_ * runtimeReduction_;
        right[i] = inR + excR * mix_ * runtimeReduction_;
    }
}

void HarmonicExciter::reset() {
    hpfL_.reset();
    hpfR_.reset();
    osLeft_.reset();
    osRight_.reset();
}

} // namespace Ivanna
EXCITER_CPP_EOF

# Synchronize top-level header copies if present
if [ -f "IvannaFusionCore.hpp" ]; then
    cp app/src/main/cpp/IvannaFusionCore.hpp ./IvannaFusionCore.hpp
fi
if [ -f "EvolutionaryEQ.hpp" ]; then
    cp app/src/main/cpp/EvolutionaryEQ.hpp ./EvolutionaryEQ.hpp
fi
if [ -f "HrtfManager.hpp" ]; then
    cp app/src/main/cpp/HrtfManager.hpp ./HrtfManager.hpp
fi
if [ -f "HrtfManager.cpp" ]; then
    cp app/src/main/cpp/HrtfManager.cpp ./HrtfManager.cpp
fi

echo "=== Git Staging, Commit & Push ==="
git add .
git commit -m "fix(dsp): align HarmonicExciter API, declare IvannaFusionCore virtual methods & resolve HrtfManager scope errors"
git push origin main

echo "=== ¡Proceso completado con éxito! ==="
