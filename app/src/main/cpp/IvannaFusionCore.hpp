#pragma once

#include <cstdint>
#include <array>
#include <memory>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#else
// Fallback type stubs if compiled on non-ARM target (e.g. x86_64 host lint)
#include <cmath>
#include <algorithm>
#endif

#define ALIGN_NEON alignas(16)

namespace Ivanna {

constexpr size_t BLOCK_SIZE = 1024;
constexpr size_t BANDS_512 = 512;
constexpr size_t FIR_TAPS = 256;
constexpr float SAMPLE_RATE = 48000.0f;

struct ALIGN_NEON AudioBuffer {
    float left[BLOCK_SIZE];
    float right[BLOCK_SIZE];
};

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
// Ultra-fast polynomial tanh approximation using NEON: x * (27 + x^2) / (27 + 9x^2)
inline float32x4_t fast_tanh_neon(float32x4_t x) {
    float32x4_t x2 = vmulq_f32(x, x);
    float32x4_t num = vmulq_f32(x, vaddq_f32(vdupq_n_f32(27.0f), x2));
    float32x4_t den = vaddq_f32(vdupq_n_f32(27.0f), vmulq_n_f32(x2, 9.0f));
    // Fast reciprocal estimation with 1 Newton-Raphson iteration
    float32x4_t rec = vrecpeq_f32(den);
    rec = vmulq_f32(vrecpsq_f32(den, rec), rec);
    return vmulq_f32(num, rec);
}
#else
inline float fast_tanh_scalar(float x) {
    float x2 = x * x;
    return (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
}
#endif

class HrtfManager;
class EvolutionaryEQ;
class Psychoacoustics;
class IvannaAudioClassifier;

class IvannaFusionEngine {
public:
    IvannaFusionEngine();
    ~IvannaFusionEngine();

    void runAcousticProfiling();
    void process(AudioBuffer* buffer);
    void setGoldenEarMode(bool enable);
    void updateHeadPose(float yaw, float pitch, float roll);
    IvannaAudioClassifier* getClassifier() const noexcept { return m_classifier; }

private:
    bool m_goldenEarActive = false;
    HrtfManager* m_hrtf = nullptr;
    EvolutionaryEQ* m_evoEq = nullptr;
    Psychoacoustics* m_psycho = nullptr;
    IvannaAudioClassifier* m_classifier = nullptr;

    void applyGoldenEarGAN(AudioBuffer* buffer);
};

} // namespace Ivanna

// ── Alias de compatibilidad ───────────────────────────────────────────────────
// El bot renombró IvannaFusionCore → IvannaFusionEngine en este header pero
// omega_effect.cpp y otros consumidores siguen usando IvannaFusionCore.
// El alias evita tocar todos los sitios de uso y mantiene la semántica clara.
namespace Ivanna {
    using IvannaFusionCore = IvannaFusionEngine;
}
