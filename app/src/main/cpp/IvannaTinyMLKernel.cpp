/**
 * ============================================================================
 * IVANNA-OMEGA-SUPREME — KERNEL-LEVEL ACOUSTIC INTELLIGENCE ENGINE
 * Componente: IvannaTinyMLKernel.cpp
 * ============================================================================
 */

#include "IvannaTinyMLKernel.hpp"
#include <algorithm>
#include <chrono>
#include <numbers>
#include <pthread.h>
#include <sched.h>

namespace ivanna::tinyml {

namespace {
// Coeficientes normalizados del filtro anti-aliasing FIR semisimétrico de 5 taps
constexpr float kAntiAliasingFir[5] = {0.0532f, 0.2468f, 0.4000f, 0.2468f, 0.0532f};

// Rango de cuantización dinámica Log-Mel -> INT8
constexpr float kLogMelFloor = -100.0f;
constexpr float kQuantScale  = 255.0f / 100.0f; // Escalar rango [0..100] a [0..255]

[[maybe_unused]] inline float clampf(float val, float minVal, float maxVal) noexcept {
    return std::max(minVal, std::min(val, maxVal));
}

inline float hzToMel(float hz) noexcept {
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

inline float melToHz(float mel) noexcept {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}
} // namespace

IvannaTinyMLAudioKernel::IvannaTinyMLAudioKernel() noexcept
    : m_cleanSlot(&m_poolSlots[0]),
      m_readingSlot(&m_poolSlots[1]),
      m_writingSlot(&m_poolSlots[2])
{
    initializeFilterbanks();

    for (auto& slot : m_poolSlots) {
        slot.targetWidenerMultiplier = 1.0f;
        slot.targetSpatialSpread     = 1.0f;
        slot.antiDolbyIntensity      = 1.0f;
        slot.dominantScene           = AcousticSceneId::Unknown;
        slot.isValid                 = false;
    }

    m_workerRunning.store(true, std::memory_order_release);
    m_inferenceWorker = std::thread(&IvannaTinyMLAudioKernel::workerInferenceLoop, this);

    sched_param param{};
    param.sched_priority = 2;
    pthread_setschedparam(m_inferenceWorker.native_handle(), SCHED_FIFO, &param);
}

IvannaTinyMLAudioKernel::~IvannaTinyMLAudioKernel() noexcept {
    m_workerRunning.store(false, std::memory_order_release);
    if (m_inferenceWorker.joinable()) {
        m_inferenceWorker.join();
    }
}

void IvannaTinyMLAudioKernel::initializeFilterbanks() noexcept {
    for (size_t i = 0; i < kFftWindowSize; ++i) {
        m_analysisWindow[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (kFftWindowSize - 1)));
    }

    std::memset(m_melFilterbank, 0, sizeof(m_melFilterbank));
    const float melMin = hzToMel(0.0f);
    const float melMax = hzToMel(8000.0f);
    const float melStep = (melMax - melMin) / static_cast<float>(kMelFilterBands + 1);

    for (size_t m = 0; m < kMelFilterBands; ++m) {
        const float centerMel = melMin + static_cast<float>(m + 1) * melStep;
        const float leftMel   = centerMel - melStep;
        const float rightMel  = centerMel + melStep;

        const float leftHz   = melToHz(leftMel);
        const float centerHz = melToHz(centerMel);
        const float rightHz  = melToHz(rightMel);

        for (size_t bin = 0; bin < kFftSpectrumBins; ++bin) {
            const float binHz = (static_cast<float>(bin) * 16000.0f) / static_cast<float>(kFftWindowSize);

            if (binHz >= leftHz && binHz <= centerHz && centerHz > leftHz) {
                m_melFilterbank[m][bin] = (binHz - leftHz) / (centerHz - leftHz);
            } else if (binHz > centerHz && binHz <= rightHz && rightHz > centerHz) {
                m_melFilterbank[m][bin] = (rightHz - binHz) / (rightHz - centerHz);
            }
        }
    }
}

void IvannaTinyMLAudioKernel::ingestPcmBlock(
    const float* __restrict left,
    const float* __restrict right,
    size_t numFrames) noexcept
{
    if (!left || !right || numFrames == 0) [[unlikely]] return;

    float decimatedStaging[256];
    size_t stagingCount = 0;

    for (size_t i = 0; i < numFrames; ++i) {
        const float mono = (left[i] + right[i]) * 0.5f;

        m_decimRing[m_decimRingPos] = mono;
        m_decimRingPos = (m_decimRingPos + 1) & 7;

        if (++m_decimPhaseCounter >= kDecimateFactor) {
            m_decimPhaseCounter = 0;

            float filteredSample = 0.0f;
            for (int k = 0; k < 5; ++k) {
                const size_t tapIdx = (m_decimRingPos - 1 - k) & 7;
                filteredSample += m_decimRing[tapIdx] * kAntiAliasingFir[k];
            }

            decimatedStaging[stagingCount++] = filteredSample;

            if (stagingCount == 256) {
                m_ringBuffer.push(decimatedStaging, stagingCount);
                stagingCount = 0;
            }
        }
    }

    if (stagingCount > 0) {
        m_ringBuffer.push(decimatedStaging, stagingCount);
    }
}

void IvannaTinyMLAudioKernel::getAcousticControl(AcousticControlVector& outControl) const noexcept {
    AcousticControlVector* published = m_cleanSlot.load(std::memory_order_acquire);
    if (published != m_readingSlot) {
        m_readingSlot = m_cleanSlot.exchange(m_readingSlot, std::memory_order_acq_rel);
    }

    if (m_readingSlot && m_readingSlot->isValid) [[likely]] {
        outControl = *m_readingSlot;
    }
}

void IvannaTinyMLAudioKernel::computeCausalMelSpectrogram(const float* __restrict inputFrame) noexcept {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < kFftWindowSize; i += 4) {
        float32x4_t vSig = vld1q_f32(&inputFrame[i]);
        float32x4_t vWin = vld1q_f32(&m_analysisWindow[i]);
        vst1q_f32(&m_stftTimeBuffer[i], vmulq_f32(vSig, vWin));
    }
#else
    for (size_t i = 0; i < kFftWindowSize; ++i) {
        m_stftTimeBuffer[i] = inputFrame[i] * m_analysisWindow[i];
    }
#endif

    for (size_t k = 0; k < kFftSpectrumBins; ++k) {
        const float sample = m_stftTimeBuffer[k];
        m_powerSpectrum[k] = sample * sample;
    }

    for (size_t m = 0; m < kMelFilterBands; ++m) {
        float energy = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t vAcc = vdupq_n_f32(0.0f);
        for (size_t k = 0; k < (kFftSpectrumBins - 1); k += 4) {
            float32x4_t vPow = vld1q_f32(&m_powerSpectrum[k]);
            float32x4_t vMel = vld1q_f32(&m_melFilterbank[m][k]);
            vAcc = vmlaq_f32(vAcc, vPow, vMel);
        }
        energy = vaddvq_f32(vAcc);
#else
        for (size_t k = 0; k < kFftSpectrumBins; ++k) {
            energy += m_powerSpectrum[k] * m_melFilterbank[m][k];
        }
#endif
        m_logMelFeatures[m] = std::log(std::max(energy, 1.0e-10f));
    }
}

void IvannaTinyMLAudioKernel::executeInt8StreamingInference() noexcept {
    alignas(16) int8_t qMel[kMelFilterBands];

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const float32x4_t vScale  = vdupq_n_f32(kQuantScale);
    const float32x4_t vFloor  = vdupq_n_f32(-kLogMelFloor);
    const float32x4_t vOffset = vdupq_n_f32(128.0f);

    for (size_t i = 0; i < kMelFilterBands; i += 4) {
        float32x4_t vLog = vld1q_f32(&m_logMelFeatures[i]);
        vLog = vaddq_f32(vLog, vFloor);
        vLog = vmulq_f32(vLog, vScale);
        vLog = vsubq_f32(vLog, vOffset);

        int32x4_t vInt32 = vcvtq_s32_f32(vLog);
        int16x4_t vInt16 = vqmovn_s32(vInt32);
        int8x8_t  vInt8  = vqmovn_s16(vcombine_s16(vInt16, vInt16));

        qMel[i + 0] = vget_lane_s8(vInt8, 0);
        qMel[i + 1] = vget_lane_s8(vInt8, 1);
        qMel[i + 2] = vget_lane_s8(vInt8, 2);
        qMel[i + 3] = vget_lane_s8(vInt8, 3);
    }
#else
    for (size_t i = 0; i < kMelFilterBands; ++i) {
        const float normalized = (m_logMelFeatures[i] - kLogMelFloor) * kQuantScale - 128.0f;
        qMel[i] = static_cast<int8_t>(clampf(normalized, -128.0f, 127.0f));
    }
#endif

    alignas(16) int32_t convFeatures[16]{0};
    for (size_t f = 0; f < 16; ++f) {
        int32_t acc = 0;
        for (size_t k = 0; k < 3; ++k) {
            const size_t inIdx = (f * 4 + k) % kMelFilterBands;
            acc += static_cast<int32_t>(qMel[inIdx]) * static_cast<int32_t>(m_weightsConv[f * 3 + k]);
        }
        convFeatures[f] = std::clamp(acc >> 3, 0, 127);
    }

    for (size_t h = 0; h < kHiddenStates; ++h) {
        int32_t net = 0;
        for (size_t f = 0; f < 16; ++f) {
            net += convFeatures[f] * m_weightsGru[h * 16 + f];
        }
        for (size_t prevH = 0; prevH < kHiddenStates; ++prevH) {
            net += m_gruHiddenState[prevH] * m_weightsGru[256 + h * kHiddenStates + prevH];
        }

        const int32_t activated = std::clamp(net >> 5, -128, 127);
        m_gruHiddenState[h] = static_cast<int8_t>(activated);
    }

    float logits[6]{0.0f};
    float maxLogit = -1e9f;
    for (size_t c = 0; c < 6; ++c) {
        int32_t logitAcc = 0;
        for (size_t h = 0; h < kHiddenStates; ++h) {
            logitAcc += m_gruHiddenState[h] * m_weightsDense[c * kHiddenStates + h];
        }
        logits[c] = static_cast<float>(logitAcc) * 0.015f;
        if (logits[c] > maxLogit) maxLogit = logits[c];
    }

    float sumExp = 0.0f;
    float bestProb = 0.0f;
    size_t dominantIdx = 0;

    for (size_t c = 0; c < 6; ++c) {
        m_writingSlot->classProbabilities[c] = std::exp(logits[c] - maxLogit);
        sumExp += m_writingSlot->classProbabilities[c];
    }

    for (size_t c = 0; c < 6; ++c) {
        m_writingSlot->classProbabilities[c] /= sumExp;
        if (m_writingSlot->classProbabilities[c] > bestProb) {
            bestProb = m_writingSlot->classProbabilities[c];
            dominantIdx = c;
        }
    }

    m_writingSlot->dominantScene = static_cast<AcousticSceneId>(dominantIdx);
    m_writingSlot->confidence    = bestProb;

    switch (m_writingSlot->dominantScene) {
        case AcousticSceneId::Voice:
            m_writingSlot->targetWidenerMultiplier = 0.75f;
            m_writingSlot->targetVocalPresenceDb   = 2.80f;
            m_writingSlot->targetSubExciterDrive   = 0.00f;
            m_writingSlot->targetSpatialSpread     = 0.90f;
            m_writingSlot->antiDolbyIntensity      = 0.85f;
            break;

        case AcousticSceneId::Music:
            m_writingSlot->targetWidenerMultiplier = 1.38f;
            m_writingSlot->targetVocalPresenceDb   = 0.00f;
            m_writingSlot->targetSubExciterDrive   = 0.40f;
            m_writingSlot->targetSpatialSpread     = 1.30f;
            m_writingSlot->antiDolbyIntensity      = 1.00f;
            break;

        case AcousticSceneId::Movie:
            m_writingSlot->targetWidenerMultiplier = 1.25f;
            m_writingSlot->targetVocalPresenceDb   = 1.60f;
            m_writingSlot->targetSubExciterDrive   = 0.75f;
            m_writingSlot->targetSpatialSpread     = 1.35f;
            m_writingSlot->antiDolbyIntensity      = 0.95f;
            break;

        case AcousticSceneId::Game:
            m_writingSlot->targetWidenerMultiplier = 1.15f;
            m_writingSlot->targetVocalPresenceDb   = 1.00f;
            m_writingSlot->targetSubExciterDrive   = 0.25f;
            m_writingSlot->targetSpatialSpread     = 1.20f;
            m_writingSlot->antiDolbyIntensity      = 0.90f;
            break;

        case AcousticSceneId::Ambient:
            m_writingSlot->targetWidenerMultiplier = 1.45f;
            m_writingSlot->targetVocalPresenceDb   = 0.00f;
            m_writingSlot->targetSubExciterDrive   = 0.10f;
            m_writingSlot->targetSpatialSpread     = 1.40f;
            m_writingSlot->antiDolbyIntensity      = 0.80f;
            break;

        default:
            m_writingSlot->targetWidenerMultiplier = 1.00f;
            m_writingSlot->targetVocalPresenceDb   = 0.00f;
            m_writingSlot->targetSubExciterDrive   = 0.00f;
            m_writingSlot->targetSpatialSpread     = 1.00f;
            m_writingSlot->antiDolbyIntensity      = 0.50f;
            break;
    }

    m_writingSlot->isValid = true;
    m_writingSlot->inferenceEpochNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    m_writingSlot = m_cleanSlot.exchange(m_writingSlot, std::memory_order_acq_rel);
}

void IvannaTinyMLAudioKernel::workerInferenceLoop() noexcept {
    float frameBuffer[kFftWindowSize];

    while (m_workerRunning.load(std::memory_order_acquire)) {
        if (m_ringBuffer.available() >= kHopLengthSamples) {
            std::memmove(&frameBuffer[0], &frameBuffer[kHopLengthSamples],
                         (kFftWindowSize - kHopLengthSamples) * sizeof(float));

            m_ringBuffer.pop(&frameBuffer[kFftWindowSize - kHopLengthSamples], kHopLengthSamples);

            computeCausalMelSpectrogram(frameBuffer);
            executeInt8StreamingInference();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

bool IvannaTinyMLAudioKernel::loadTrainedModelWeights(std::span<const uint8_t> binaryBlob) noexcept {
    constexpr size_t kTotalExpectedBytes = sizeof(m_weightsConv) + sizeof(m_weightsGru) +
                                           sizeof(m_weightsDense) + sizeof(m_weightsRegression);
    if (binaryBlob.size() < kTotalExpectedBytes) return false;

    const uint8_t* ptr = binaryBlob.data();
    std::memcpy(m_weightsConv, ptr, sizeof(m_weightsConv)); ptr += sizeof(m_weightsConv);
    std::memcpy(m_weightsGru, ptr, sizeof(m_weightsGru)); ptr += sizeof(m_weightsGru);
    std::memcpy(m_weightsDense, ptr, sizeof(m_weightsDense)); ptr += sizeof(m_weightsDense);
    std::memcpy(m_weightsRegression, ptr, sizeof(m_weightsRegression));

    m_isModelLoaded.store(true, std::memory_order_release);
    return true;
}

} // namespace ivanna::tinyml
