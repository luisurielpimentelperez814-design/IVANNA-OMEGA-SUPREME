#include "IvannaVoiceProsodyEngine.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

IvannaVoiceProsodyEngine::IvannaVoiceProsodyEngine() {
    std::fill(m_monoDownmix, m_monoDownmix + MAX_FRAME, 0.0f);
}

void IvannaVoiceProsodyEngine::analyzeAudio(const float* left, const float* right, size_t frames) noexcept {
    if (frames == 0 || frames > MAX_FRAME) return;

    // 1. Mono downmix & energy calculation via NEON
    float energy = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t vEnergy = vdupq_n_f32(0.0f);
    size_t i = 0;
    for (; i + 3 < frames; i += 4) {
        float32x4_t vL = vld1q_f32(&left[i]);
        float32x4_t vR = vld1q_f32(&right[i]);
        float32x4_t vMono = vmulq_n_f32(vaddq_f32(vL, vR), 0.5f);
        vst1q_f32(&m_monoDownmix[i], vMono);
        vEnergy = vmlaq_f32(vEnergy, vMono, vMono);
    }
    float energyArr[4];
    vst1q_f32(energyArr, vEnergy);
    energy = energyArr[0] + energyArr[1] + energyArr[2] + energyArr[3];
    for (; i < frames; ++i) {
        m_monoDownmix[i] = (left[i] + right[i]) * 0.5f;
        energy += m_monoDownmix[i] * m_monoDownmix[i];
    }
#else
    for (size_t i = 0; i < frames; ++i) {
        m_monoDownmix[i] = (left[i] + right[i]) * 0.5f;
        energy += m_monoDownmix[i] * m_monoDownmix[i];
    }
#endif

    energy /= static_cast<float>(frames);
    
    // VAD Gate
    bool isVoiced = (energy > 1e-5f); // Rough threshold ~ -50dBFS
    m_voiced.store(isVoiced, std::memory_order_relaxed);

    if (isVoiced) {
        float pitch = computeAMDFPitch(m_monoDownmix, frames);
        m_pitchHz.store(pitch, std::memory_order_relaxed);
        m_confidence.store(pitch > 50.0f ? 0.85f : 0.2f, std::memory_order_relaxed);
        
        // Pseudo spectral tilt based on zero-crossing derivative approximation
        float diffSum = 0.0f;
        for (size_t k = 1; k < frames; ++k) {
            diffSum += std::abs(m_monoDownmix[k] - m_monoDownmix[k-1]);
        }
        m_tilt.store(diffSum / (frames * std::sqrt(energy) + 1e-9f), std::memory_order_relaxed);
    } else {
        m_confidence.store(0.0f, std::memory_order_relaxed);
    }
}

float IvannaVoiceProsodyEngine::computeAMDFPitch(const float* buffer, size_t frames) noexcept {
    // ════════════════════════════════════════════════════════════════════════
    // Fast AMDF (Average Magnitude Difference Function)
    // Searches lag corresponding to human pitch (70Hz - 300Hz @ 48kHz SR)
    // Lag range: 160 (300Hz) to 685 (70Hz)
    // ════════════════════════════════════════════════════════════════════════
    const int minLag = 160;
    const int maxLag = std::min((int)frames / 2, 685);
    
    if (maxLag <= minLag) return 0.0f;

    float minDiff = 1e9f;
    int bestLag = -1;

    for (int lag = minLag; lag < maxLag; ++lag) {
        float diff = 0.0f;
        int compareFrames = frames - lag;
        
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t vDiff = vdupq_n_f32(0.0f);
        int j = 0;
        for (; j + 3 < compareFrames; j += 4) {
            float32x4_t v0 = vld1q_f32(&buffer[j]);
            float32x4_t v1 = vld1q_f32(&buffer[j + lag]);
            float32x4_t vAbsDiff = vabsq_f32(vsubq_f32(v0, v1));
            vDiff = vaddq_f32(vDiff, vAbsDiff);
        }
        float diffArr[4];
        vst1q_f32(diffArr, vDiff);
        diff = diffArr[0] + diffArr[1] + diffArr[2] + diffArr[3];
        for (; j < compareFrames; ++j) {
            diff += std::abs(buffer[j] - buffer[j + lag]);
        }
#else
        for (int j = 0; j < compareFrames; ++j) {
            diff += std::abs(buffer[j] - buffer[j + lag]);
        }
#endif
        diff /= compareFrames;
        if (diff < minDiff) {
            minDiff = diff;
            bestLag = lag;
        }
    }

    if (bestLag > 0) {
        return 48000.0f / static_cast<float>(bestLag);
    }
    return 0.0f;
}

ProsodyMetrics IvannaVoiceProsodyEngine::getMetrics() const noexcept {
    return {
        m_pitchHz.load(std::memory_order_relaxed),
        m_confidence.load(std::memory_order_relaxed),
        m_tilt.load(std::memory_order_relaxed),
        m_voiced.load(std::memory_order_relaxed)
    };
}

} // namespace Ivanna
