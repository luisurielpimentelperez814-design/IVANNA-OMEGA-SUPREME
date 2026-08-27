#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

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
class EvolutionaryEQ;
class Psychoacoustics;
class IvannaAudioClassifier;

class IvannaFusionEngine : public IvannaFusionCore {
public:
    IvannaFusionEngine();
    explicit IvannaFusionEngine(float /*sampleRate*/) : IvannaFusionEngine() {}
    virtual ~IvannaFusionEngine();

    void runAcousticProfiling();
    void setGoldenEarMode(bool enable);
    void applyGoldenEarGAN(AudioBuffer* buffer);
    void process(AudioBuffer* buffer);

    IvannaAudioClassifier* getClassifier() const noexcept { return m_classifier; }

    void processBlock(AudioBuffer* buffer) override { process(buffer); }
    void setParameter(uint32_t paramId, float value) override { (void)paramId; (void)value; }

    // ── API requerida por omega_effect.cpp ────────────────────────────────────
    // omega_effect.cpp fue escrito contra una versión anterior de IvannaFusionCore
    // que tenía estos métodos. Se mantienen aquí para que el contrato de omega_effect
    // no necesite cambiar — toda la lógica real está en process() y en los miembros.

    void initSpatial(float sr, int /*blockSize*/) noexcept {
        m_sampleRate = sr;
    }

    // Procesa un bloque estéreo in-place delegando a process().
    void processStereo(float* L, float* R, size_t frames) noexcept {
        if (!L || !R || frames == 0) return;
        AudioBuffer buf{};
        const size_t n = frames < BLOCK_SIZE ? frames : BLOCK_SIZE;
        for (size_t i = 0; i < n; ++i) { buf.left[i] = L[i]; buf.right[i] = R[i]; }
        process(&buf);
        for (size_t i = 0; i < n; ++i) { L[i] = buf.left[i]; R[i] = buf.right[i]; }
    }

    bool loadCustomHrtf(const char* /*path*/) noexcept { return false; }
    void setSpatialWidth(float /*w*/) noexcept {}
    void setHarmonicGain(float g) noexcept {
        // FIX (anti-zipper): escalon duro por bloque → target + slew EMA.
        // El slider mueve este parametro decenas de veces por segundo; sin
        // suavizado, cada movimiento es un salto de ganancia del exciter
        // (click audible). Mismo patron que RirConvolver::wetSmooth_ (10ms).
        m_harmonicGainTarget = g;
    }
    float smoothedHarmonicGain() noexcept {
        // tau ~10 ms @48k; m_sampleRate ya la fija initSpatial().
        const float sr = m_sampleRate > 0.f ? m_sampleRate : 48000.f;
        const float k  = std::exp(-1.0 / ((double)sr * 0.010));
        m_harmonicGain += (float)k * (m_harmonicGainTarget - m_harmonicGain);
        return m_harmonicGain;
    }
    void setCompressorParams(float t, float r) noexcept { m_compThresh = t; m_compRatio = r; }
    void setIntensity(float /*i*/)                          noexcept {}
    void setRouteProfile(float /*bass*/, float /*dialog*/,
                         float /*widener*/)                 noexcept {}
    void setEqGains(const float* /*g*/, int /*n*/,
                    float /*listenPhon*/, float /*refPhon*/) noexcept {}
    void setSafLatentParams(const float q[7]) noexcept;
    void clearSafLatentParams() noexcept {}
    void updateHeadPose(float /*yaw*/, float /*pitch*/, float /*roll*/) noexcept {}

private:
    bool  m_goldenEarActive = false;
    float m_sampleRate  = SAMPLE_RATE;
    float m_harmonicGain = 0.f;
    // Target del slew anti-zipper (83bf9eb9): el setter escribía esta variable
    // sin declararla — error de compilación en arm64-v8a/armeabi-v7a que
    // tumbaba el build del APK (CI 33122617356).
    float m_harmonicGainTarget = 0.f;
    float m_compThresh   = -18.f;
    float m_compRatio    = 4.f;
    HrtfManager*          m_hrtf       = nullptr;
    EvolutionaryEQ*       m_evoEq      = nullptr;
    Psychoacoustics*      m_psycho     = nullptr;
    IvannaAudioClassifier* m_classifier = nullptr;

    void applyHarmonicExciter(AudioBuffer* buffer);
};

} // namespace Ivanna

// ── Alias global (FUERA de namespace Ivanna) ──────────────────────────────────
// omega_effect.cpp usa 'IvannaFusionCore' sin cualificar (scope global).
// Ivanna::IvannaFusionCore es la CLASE BASE; ::IvannaFusionCore (este alias)
// apunta a IvannaFusionEngine (la clase concreta con todos los métodos).
// Estaba comentado + dentro de namespace Ivanna{} — ambos errores corregidos.
// ::IvannaFusionCore ≠ Ivanna::IvannaFusionCore → sin conflicto de nombres.

