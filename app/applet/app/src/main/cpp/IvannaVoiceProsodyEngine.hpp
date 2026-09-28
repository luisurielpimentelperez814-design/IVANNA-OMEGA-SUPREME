#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define ALIGN_NEON alignas(16)
#else
#define ALIGN_NEON alignas(16)
#endif

namespace Ivanna {

struct ProsodyMetrics {
    float pitchFreq;         // Fundamental frequency (F0) in Hz
    float pitchConfidence;   // [0.0, 1.0]
    float spectralTilt;      // Indication of vocal effort
    bool  isVoiced;          // Voice Activity Detection (VAD) flag
};

/**
 * ════════════════════════════════════════════════════════════════════════
 * IVANNA VOICE PROSODY ENGINE (DSP-Accelerated)
 * ════════════════════════════════════════════════════════════════════════
 * Real-time F0 tracking and VAD using an optimized AMDF (Average Magnitude 
 * Difference Function) combined with spectral centroid/tilt heuristics.
 * Implemented with ARM NEON for zero-latency in-band processing.
 * ════════════════════════════════════════════════════════════════════════
 */
class ALIGN_NEON IvannaVoiceProsodyEngine {
public:
    IvannaVoiceProsodyEngine();
    ~IvannaVoiceProsodyEngine() = default;

    // Analyze a raw mono/stereo block. Lock-free, Wait-free.
    void analyzeAudio(const float* left, const float* right, size_t frames) noexcept;

    // Returns latest computed metrics atomically
    ProsodyMetrics getMetrics() const noexcept;

private:
    static constexpr size_t MAX_FRAME = 512;
    
    ALIGN_NEON float m_monoDownmix[MAX_FRAME];
    
    std::atomic<float> m_pitchHz{0.0f};
    std::atomic<float> m_confidence{0.0f};
    std::atomic<float> m_tilt{0.0f};
    std::atomic<bool>  m_voiced{false};

    float computeAMDFPitch(const float* buffer, size_t frames) noexcept;
};

} // namespace Ivanna
