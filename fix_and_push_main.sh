#!/usr/bin/env bash
set -e

# 1. Corregir IvannaFusionCore.hpp (Añadir <cstddef> para size_t y constantes globales)
cat <<'CEOF' > app/src/main/cpp/IvannaFusionCore.hpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <cmath>

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 128;
constexpr size_t FIR_TAPS = 256;
constexpr size_t BANDS_512 = 512;

struct AudioBuffer {
    alignas(16) float left[BLOCK_SIZE];
    alignas(16) float right[BLOCK_SIZE];
};

inline float fast_tanh_neon(float x) {
    float x2 = x * x;
    float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
}

} // namespace Ivanna
CEOF

# 2. Corregir EvolutionaryEQ.hpp (Inclusión de <cstddef> e IvannaFusionCore.hpp)
cat <<'CEOF' > app/src/main/cpp/EvolutionaryEQ.hpp
#pragma once

#include <cstddef>
#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    ~EvolutionaryEQ() = default;

    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES();

private:
    alignas(16) float m_firCoeffsL[FIR_TAPS];
    alignas(16) float m_firCoeffsR[FIR_TAPS];

    alignas(16) float m_histL[BLOCK_SIZE + FIR_TAPS];
    alignas(16) float m_histR[BLOCK_SIZE + FIR_TAPS];

    float m_meanGenome[BANDS_512];
    float m_evolutionPath[BANDS_512];
    float m_stepSize{0.1f};

    float calculateFitness(const float* genome);
};

} // namespace Ivanna
CEOF

# 3. Corregir EvolutionaryEQ.cpp (Declaración completa de includes y métodos)
cat <<'CEOF' > app/src/main/cpp/EvolutionaryEQ.cpp
#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include <cstddef>
#include <cmath>
#include <random>

namespace Ivanna {

static thread_local std::mt19937 g_rng(1337);

static inline float fast_rand() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(g_rng);
}

EvolutionaryEQ::EvolutionaryEQ() {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_firCoeffsL[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
        m_firCoeffsR[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
    }

    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] = 0.0f;
        m_evolutionPath[i] = 0.0f;
    }
}

float EvolutionaryEQ::calculateFitness(const float* genome) {
    float smoothnessPenalty = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        float diff = genome[i] - genome[i - 1];
        smoothnessPenalty += diff * diff;
    }
    return -smoothnessPenalty;
}

void EvolutionaryEQ::updateLM_CMA_ES() {
    constexpr size_t lambda = 8;
    float population[lambda][BANDS_512];
    float fitness[lambda];

    for (size_t p = 0; p < lambda; ++p) {
        for (size_t i = 0; i < BANDS_512; ++i) {
            population[p][i] = m_meanGenome[i] + m_stepSize * fast_rand();
        }
        fitness[p] = calculateFitness(population[p]);
    }

    size_t best_idx = 0;
    float max_fit = fitness[0];
    for (size_t p = 1; p < lambda; ++p) {
        if (fitness[p] > max_fit) {
            max_fit = fitness[p];
            best_idx = p;
        }
    }

    constexpr float cc = 0.2f;
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] += 0.5f * (population[best_idx][i] - m_meanGenome[i]);
        m_evolutionPath[i] = (1.0f - cc) * m_evolutionPath[i] + cc * m_meanGenome[i];
    }
}

void EvolutionaryEQ::processNEON(AudioBuffer* buffer) {
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float l_out = 0.0f;
        float r_out = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            l_out += m_firCoeffsL[t] * m_histL[i + t];
            r_out += m_firCoeffsR[t] * m_histR[i + t];
        }

        buffer->left[i] = fast_tanh_neon(l_out);
        buffer->right[i] = fast_tanh_neon(r_out);
    }

    for (size_t i = 0; i < FIR_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
CEOF

# 4. Eliminar advertencia de variable no usada en HarmonicExciter.cpp
cat <<'CEOF' > app/src/main/cpp/dsp/HarmonicExciter.cpp
#include "dsp/HarmonicExciter.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

HarmonicExciter::HarmonicExciter() {
    reset();
}

void HarmonicExciter::reset() {
    stateL_ = 0.0f;
    stateR_ = 0.0f;
    lpStateL_ = 0.0f;
    lpStateR_ = 0.0f;
    dcStateL_ = 0.0f;
    dcStateR_ = 0.0f;
    osBufferL_.fill(0.0f);
    osBufferR_.fill(0.0f);
}

void HarmonicExciter::setHarmonics(float h2, float h3, float h4) {
    h2_ = std::clamp(h2, 0.0f, 2.0f);
    h3_ = std::clamp(h3, 0.0f, 2.0f);
    h4_ = std::clamp(h4, 0.0f, 2.0f);
}

void HarmonicExciter::setMix(float drive, float wet, float dry) {
    drive_ = std::clamp(drive, 0.0f, 4.0f);
    wet_   = std::clamp(wet,   0.0f, 1.0f);
    dry_   = std::clamp(dry,   0.0f, 1.0f);
}

void HarmonicExciter::setSafetyGuard(float currentFatigueIndex, float acousticPressure) {
    if (currentFatigueIndex > 0.8f || acousticPressure > 0.9f) {
        runtimeReductionMul_ = 0.3f;
    } else if (currentFatigueIndex > 0.5f) {
        runtimeReductionMul_ = 0.6f;
    } else {
        runtimeReductionMul_ = 1.0f;
    }
}

void HarmonicExciter::process(float* left, float* right, int numSamples) {
    const float drive = drive_;
    const float wet   = wet_ * runtimeReductionMul_;
    const float dry   = dry_;
    (void)dry;

    for (int i = 0; i < numSamples; ++i) {
        float inL = left[i];
        float inR = right[i];

        float hpL = inL - lpStateL_;
        float hpR = inR - lpStateR_;
        lpStateL_ += 0.15f * (inL - lpStateL_);
        lpStateR_ += 0.15f * (inR - lpStateR_);

        float xL = hpL * (1.0f + drive);
        float xR = hpR * (1.0f + drive);

        float harmL = h2_ * (xL * xL) + h3_ * (xL * xL * xL) + h4_ * (xL * xL * xL * xL);
        float harmR = h2_ * (xR * xR) + h3_ * (xR * xR * xR) + h4_ * (xR * xR * xR * xR);

        harmL -= dcStateL_;
        harmR -= dcStateR_;
        dcStateL_ += 0.01f * harmL;
        dcStateR_ += 0.01f * harmR;

        left[i]  = inL + harmL * wet;
        right[i] = inR + harmR * wet;
    }
}

} // namespace ivanna
CEOF

# 5. Replicar cabeceras a la raíz para compatibilidad con builds standalone
cp -f app/src/main/cpp/IvannaFusionCore.hpp ./IvannaFusionCore.hpp
cp -f app/src/main/cpp/EvolutionaryEQ.hpp ./EvolutionaryEQ.hpp
cp -f app/src/main/cpp/EvolutionaryEQ.cpp ./EvolutionaryEQ.cpp

# 6. Commit y Push directo a la rama main
git add .
git commit -m "fix(dsp): resolve FIR_TAPS scope, missing headers and unused variable warnings in EvolutionaryEQ & HarmonicExciter"
#git push origin main
