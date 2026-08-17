#!/usr/bin/env bash
set -e

echo "==> Aplicando parches de kernel C++ DSP..."

# 1. HrtfManager.hpp - Declaración de buffers de historial y respuesta a impulsos HRTF
cat << 'HRTF_EOF' > app/src/main/cpp/HrtfManager.hpp
#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class HrtfManager {
public:
    HrtfManager();
    ~HrtfManager() = default;

    void updateListenerPosition(float azimuth, float elevation);
    void processSpatialization(AudioBuffer* buffer);

private:
    float m_azimuth = 0.0f;
    float m_elevation = 0.0f;

    // Buffers de convolución HRTF en el dominio del tiempo
    float m_histL[BLOCK_SIZE * 2] = {0.0f};
    float m_histR[BLOCK_SIZE * 2] = {0.0f};

    float m_hrtfLL[BLOCK_SIZE] = {0.0f};
    float m_hrtfRL[BLOCK_SIZE] = {0.0f};
    float m_hrtfRR[BLOCK_SIZE] = {0.0f};
    float m_hrtfLR[BLOCK_SIZE] = {0.0f};
};

} // namespace Ivanna
HRTF_EOF

# 2. IvannaFusionCore.hpp - Clase base virtual pura y alias de frecuencia de muestreo
cat << 'CORE_EOF' > app/src/main/cpp/IvannaFusionCore.hpp
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ALIGN_NEON alignas(16)
#else
#define ALIGN_NEON
#endif

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 128;
constexpr size_t FFT_SIZE = 512;
constexpr size_t BANDS_512 = 512;
constexpr size_t FIR_TAPS = 256;
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float SAMPLING_RATE = 48000.0f; // Alias para compatibilidad con clasificadores legacy

struct ALIGN_NEON AudioBuffer {
    float left[BLOCK_SIZE];
    float right[BLOCK_SIZE];
};

class HrtfManager;
class Psychoacoustics;
class IvannaAudioClassifier;

class IvannaFusionCore {
public:
    IvannaFusionCore() = default;
    virtual ~IvannaFusionCore() = default;

    virtual void processBlock(AudioBuffer* buffer) { (void)buffer; }
    virtual void setParameter(uint32_t paramId, float value) { (void)paramId; (void)value; }
};

class IvannaFusionEngine : public IvannaFusionCore {
public:
    IvannaFusionEngine();
    virtual ~IvannaFusionEngine();

    void runAcousticProfiling();
    void process(AudioBuffer* buffer);

    HrtfManager* getHrtfManager() const noexcept { return m_hrtf; }
    Psychoacoustics* getPsychoacoustics() const noexcept { return m_psycho; }
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

# 3. EvolutionaryEQ.hpp - Herencia polimórfica e implementación de overrides
cat << 'EQ_EOF' > app/src/main/cpp/EvolutionaryEQ.hpp
#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ : public IvannaFusionCore {
public:
    EvolutionaryEQ();
    void calibrateTargetRoom();
    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES(); // Pasos evolutivos CMA-ES

    void processBlock(AudioBuffer* buffer) override { processNEON(buffer); }
    void setParameter(uint32_t paramId, float value) override { (void)paramId; (void)value; }

private:
    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];
    float m_fitnessScore = 0.0f;
};

} // namespace Ivanna
EQ_EOF

# 4. HarmonicExciter.cpp - Implementación alineada a la nueva API (HarmonicExciter.h y DSPParams)
cat << 'EXC_EOF' > app/src/main/cpp/dsp/HarmonicExciter.cpp
#include "HarmonicExciter.h"
#include <algorithm>
#include <cmath>

HarmonicExciter::HarmonicExciter() {
    reset();
}

void HarmonicExciter::setParams(const DSPParams& params) {
    drive_ = params.exciterDrive;
    harmonicsMix_ = params.exciterMix;
    setHighpassCutoff(params.exciterCutoffHz, params.sampleRate);
}

void HarmonicExciter::setHighpassCutoff(float cutoffHz, float sampleRate) {
    if (sampleRate <= 0.0f) return;
    hpfL_.setHighpass(cutoffHz, sampleRate);
    hpfR_.setHighpass(cutoffHz, sampleRate);
}

void HarmonicExciter::setRuntimeReduction(float factor) {
    runtimeReduction_ = std::clamp(factor, 0.0f, 1.0f);
}

void HarmonicExciter::reset() {
    hpfL_.reset();
    hpfR_.reset();
    osLeft_.reset();
    osRight_.reset();
}

void HarmonicExciter::process(float* inL, float* inR, float* outL, float* outR, size_t numFrames) {
    float effDrive = drive_ * (1.0f - 0.5f * runtimeReduction_);
    float effMix = harmonicsMix_ * (1.0f - 0.5f * runtimeReduction_);

    for (size_t i = 0; i < numFrames; ++i) {
        float hpL = hpfL_.process(inL[i]);
        float hpR = hpfR_.process(inR[i]);

        // Sobremuestreo x2 para mitigar aliasing armónico
        float osInL[2] = { hpL, 0.0f };
        float osInR[2] = { hpR, 0.0f };
        float osOutL[2];
        float osOutR[2];

        osLeft_.upsample2x(osInL, osOutL);
        osRight_.upsample2x(osInR, osOutR);

        for (int k = 0; k < OS_FACTOR; ++k) {
            float xL = osOutL[k] * effDrive;
            float xR = osOutR[k] * effDrive;

            // Saturación asimétrica para armónicos pares e impares
            osOutL[k] = std::tanh(xL + 0.1f * xL * xL);
            osOutR[k] = std::tanh(xR + 0.1f * xR * xR);
        }

        float excL = osLeft_.downsample2x(osOutL);
        float excR = osRight_.downsample2x(osOutR);

        outL[i] = inL[i] + excL * effMix;
        outR[i] = inR[i] + excR * effMix;
    }
}
EXC_EOF

echo "==> Añadiendo cambios a Git..."
git add app/src/main/cpp/HrtfManager.hpp \
        app/src/main/cpp/IvannaFusionCore.hpp \
        app/src/main/cpp/EvolutionaryEQ.hpp \
        app/src/main/cpp/dsp/HarmonicExciter.cpp

echo "==> Creando commit..."
git commit -m "fix(dsp): resolve C++ compilation errors, virtualize IvannaFusionCore & modernize HarmonicExciter API"

echo "==> Enviando directo a main..."
git push origin main

echo "==> ¡Proceso completado exitosamente!"
