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

    // ── Constructor de compatibilidad ────────────────────────────────────────
    // omega_effect.cpp crea la instancia con un sampleRate capturado de
    // AudioFlinger (ej. 48000 Hz). El constructor delegante lo acepta sin
    // almacenarlo; la tasa de muestreo real la gestiona el engine internamente.
    explicit IvannaFusionEngine(float /*sampleRate*/) : IvannaFusionEngine() {}

    void runAcousticProfiling();
    void process(AudioBuffer* buffer);
    void setGoldenEarMode(bool enable);
    void updateHeadPose(float yaw, float pitch, float roll);

    // ── API de compatibilidad con omega_effect.cpp / OmegaControlBus ─────────
    // Estos métodos reciben los parámetros del Control Plane (snapshot SHM)
    // y los enrutan a los subsistemas internos del engine. Los marcados TODO
    // son stubs que permiten la compilación mientras se cablea el puente
    // OmegaControlBus → HrtfManager / EvolutionaryEQ / Psychoacoustics.

    // Inicialización espacial tras SET_CONFIG de AudioFlinger.
    void initSpatial(float /*sr*/, int /*blockSize*/) noexcept {
        runAcousticProfiling();
    }

    // Procesa N frames estéreo desinterleaved L/R en chunks de BLOCK_SIZE.
    // Llamado desde omega_process() en la ruta caliente de AudioFlinger.
    void processStereo(float* left, float* right, size_t frames) noexcept {
        size_t offset = 0;
        while (offset < frames) {
            size_t chunk = frames - offset;
            if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
            AudioBuffer buf{};
            __builtin_memcpy(buf.left,  left  + offset, chunk * sizeof(float));
            __builtin_memcpy(buf.right, right + offset, chunk * sizeof(float));
            process(&buf);
            __builtin_memcpy(left  + offset, buf.left,  chunk * sizeof(float));
            __builtin_memcpy(right + offset, buf.right, chunk * sizeof(float));
            offset += chunk;
        }
    }

    // Carga un dataset HRTF medido en formato IHR1 desde disco.
    // Retorna false si el archivo no existe o la cabecera es inválida.
    // TODO: delegar a HrtfManager::loadIhr1(path).
    bool loadCustomHrtf(const char* /*path*/) noexcept { return false; }

    // Parámetros del snapshot OmegaDspSnapshot → subsistemas internos.
    // TODO(OmegaControlBus-v2): delegar a StereoWidener / HarmonicExciter.
    void setSpatialWidth(float /*width*/) noexcept {}
    void setHarmonicGain(float /*gain*/) noexcept {}
    void setCompressorParams(float /*thresholdDb*/, float /*ratio*/) noexcept {}
    void setRouteProfile(float /*bassDb*/, float /*dialogDb*/,
                         float /*widener*/) noexcept {}
    void setEqGains(const float* /*gains*/, int /*n*/,
                    float /*listenPhon*/, float /*refPhon*/) noexcept {}
    void setIntensity(float /*intensity*/) noexcept {}
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
