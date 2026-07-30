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
