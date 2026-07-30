#!/bin/bash
set -e

echo "[+] Aplicando parches de compilación para IVANNA-OMEGA-SUPREME..."

# 1. Asegurar alias de constantes en IvannaFusionCore.hpp
cat << 'CORE_EOF' > app/src/main/cpp/IvannaFusionCore.hpp
#pragma once
#include <cstddef>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 256;
constexpr size_t BANDS_512 = 512;
constexpr size_t FIR_TAPS = 256;
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float SAMPLING_RATE = 48000.0f;

struct ALIGN_NEON AudioBuffer {
    float left[BLOCK_SIZE];
    float right[BLOCK_SIZE];
};

} // namespace Ivanna
CORE_EOF

# 2. Corregir espacio de nombres y miembros en HrtfManager.cpp
cat << 'HRTF_EOF' > app/src/main/cpp/HrtfManager.cpp
#include "HrtfManager.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

HrtfManager::HrtfManager() {
    m_hrtfLL.fill(0.0f);
    m_hrtfLR.fill(0.0f);
    m_hrtfRL.fill(0.0f);
    m_hrtfRR.fill(0.0f);

    m_hrtfLL[0] = 1.0f;
    m_hrtfRR[0] = 1.0f;

    m_histL.fill(0.0f);
    m_histR.fill(0.0f);
}

void HrtfManager::setAzimuthElevation(float azimuthDeg, float elevationDeg) noexcept {
    float rad = azimuthDeg * (3.14159265358979323846f / 180.0f);
    float pan = 0.5f * (std::sin(rad) + 1.0f);

    m_hrtfLL[0] = std::cos(pan * 1.57079632679f);
    m_hrtfRR[0] = std::sin(pan * 1.57079632679f);
    m_hrtfLR[0] = 0.1f * m_hrtfRR[0];
    m_hrtfRL[0] = 0.1f * m_hrtfLL[0];
}

void HrtfManager::processSpatial(const float* inL, const float* inR, float* outL, float* outR, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        m_histL[BLOCK_SIZE + i] = inL[i];
        m_histR[BLOCK_SIZE + i] = inR[i];
    }

    for (size_t i = 0; i < numSamples; ++i) {
        float accL = 0.0f;
        float accR = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            float xL = m_histL[i + t];
            float xR = m_histR[i + t];

            accL += xL * m_hrtfLL[t] + xR * m_hrtfRL[t];
            accR += xR * m_hrtfRR[t] + xL * m_hrtfLR[t];
        }

        outL[i] = accL;
        outR[i] = accR;
    }

    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
HRTF_EOF

# 3. Eliminar redefinición duplicada de SAMPLE_RATE en IvannaAudioClassifier.hpp
cat << 'CLASSIFIER_H_EOF' > app/src/main/cpp/IvannaAudioClassifier.hpp
#pragma once

#include "IvannaFusionCore.hpp"
#include <atomic>
#include <cstdint>
#include <array>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#ifndef ALIGN_NEON
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {

constexpr size_t MEL_BANDS = 64;
constexpr size_t CLASSIFIER_FRAME_SIZE = 512;
constexpr size_t FFT_SPECTRUM_SIZE = (CLASSIFIER_FRAME_SIZE / 2) + 1;
constexpr size_t CONV_CHANNELS = 32;
constexpr size_t NUM_CLASSES = 4;
constexpr size_t RING_BUFFER_CAPACITY = 16384;
constexpr float PI_F = 3.14159265358979323846f;

template <typename T, size_t Capacity>
class alignas(64) LockFreeAudioRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    LockFreeAudioRingBuffer() : m_head(0), m_tail(0) {}

    inline bool push(const T* src, size_t count) noexcept {
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        const size_t current_tail = m_tail.load(std::memory_order_acquire);
        if ((current_head + count - current_tail) > Capacity) return false;

        for (size_t i = 0; i < count; ++i) {
            m_buffer[(current_head + i) & (Capacity - 1)] = src[i];
        }
        m_head.store(current_head + count, std::memory_order_release);
        return true;
    }

    inline bool pop(T* dst, size_t count) noexcept {
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        const size_t current_head = m_head.load(std::memory_order_acquire);
        if (current_head - current_tail < count) return false;

        for (size_t i = 0; i < count; ++i) {
            dst[i] = m_buffer[(current_tail + i) & (Capacity - 1)];
        }
        m_tail.store(current_tail + count, std::memory_order_release);
        return true;
    }

    size_t available() const noexcept {
        return m_head.load(std::memory_order_relaxed) - m_tail.load(std::memory_order_relaxed);
    }

private:
    T m_buffer[Capacity];
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

class alignas(64) IvannaAudioClassifier {
public:
    IvannaAudioClassifier();
    ~IvannaAudioClassifier() = default;

    void ingestAudioFrame(const float* inputLeft, const float* inputRight, size_t numSamples) noexcept;
    void processInference() noexcept;

    const float* getClassProbabilities() const noexcept { return m_probabilities; }
    const float* getProbabilities() const noexcept { return m_probabilities; }
    uint8_t getDominantClass() const noexcept { return m_dominantClass; }

private:
    LockFreeAudioRingBuffer<float, RING_BUFFER_CAPACITY> m_audioRingBuffer;

    ALIGN_NEON float m_frameBuffer[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_windowedFrame[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_powerSpectrum[FFT_SPECTRUM_SIZE];
    ALIGN_NEON float m_melLogEnergies[MEL_BANDS];

    ALIGN_NEON float m_hanningWindow[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_fftTwiddleReal[CLASSIFIER_FRAME_SIZE / 2];
    ALIGN_NEON float m_fftTwiddleImag[CLASSIFIER_FRAME_SIZE / 2];
    uint16_t m_bitRevTable[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float m_melFilterbank[MEL_BANDS][FFT_SPECTRUM_SIZE];

    ALIGN_NEON float m_depthwiseKernel[MEL_BANDS];
    ALIGN_NEON float m_convDepthwiseWeights[MEL_BANDS];
    ALIGN_NEON float m_pointwiseWeights[CONV_CHANNELS][MEL_BANDS];
    ALIGN_NEON float m_convPointwiseWeights[CONV_CHANNELS][MEL_BANDS];
    ALIGN_NEON float m_pointwiseBiases[CONV_CHANNELS];
    ALIGN_NEON float m_denseWeights[NUM_CLASSES][CONV_CHANNELS];
    ALIGN_NEON float m_denseBiases[NUM_CLASSES];

    ALIGN_NEON float m_probabilities[NUM_CLASSES];
    uint8_t m_dominantClass = 1;

    void initFilterbankAndWindow() noexcept;
    void computeSTFT(const float* frame) noexcept;
    void extractLogMelFilterbank() noexcept;
    void extractLogMelFilterbank(const float* frame) noexcept {
        computeSTFT(frame);
        extractLogMelFilterbank();
    }
};

} // namespace Ivanna
CLASSIFIER_H_EOF

# 4. Enviar commit directamente a la rama main
git add app/src/main/cpp/IvannaFusionCore.hpp app/src/main/cpp/HrtfManager.cpp app/src/main/cpp/IvannaAudioClassifier.hpp
git commit -m "fix(dsp): resolve undeclared symbols, HRTF namespace and sample rate definition"
git push origin main

echo "[✔] Reparación aplicada y enviada exitosamente a main."
