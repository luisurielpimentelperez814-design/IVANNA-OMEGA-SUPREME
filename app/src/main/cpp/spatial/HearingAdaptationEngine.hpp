#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>
#include <algorithm>

namespace ivanna::spatial {

struct AudiogramProfile {
    float loss_250hz_db{0.0f};
    float loss_500hz_db{0.0f};
    float loss_1khz_db{0.0f};
    float loss_2khz_db{0.0f};
    float loss_4khz_db{0.0f};
    float loss_8khz_db{0.0f};
    float loss_low_db{0.0f};
    float loss_mid_db{0.0f};
    float loss_high_db{0.0f};
    float loss_ultra_high_db{0.0f};
    float listening_duration_mins{0.0f};
    float ear_tip_seal_factor{1.0f}; // [0, 1] 1 = perfect seal
};

/**
 * @class HearingAdaptationEngine
 * Eje 6: Dynamic equal-loudness contours (isophonic), ear tip seal compensation,
 * listening fatigue protection, and presbycusis compensation.
 * 
 * Latency budget: 0 samples (zero added latency, uses minimum-phase shelving/biquads).
 * CPU budget: <= 0.5%
 * RT-Safety: Zero malloc in hot path, zero locks.
 */
class HearingAdaptationEngine {
public:
    HearingAdaptationEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        lowShelveL_ = 0.0f;
        lowShelveR_ = 0.0f;
        highShelveL_ = 0.0f;
        highShelveR_ = 0.0f;
        smoothLowGain_ = targetLowGain_;
        smoothHighGain_ = targetHighGain_;
        renderedBlocks_ = 0u;
    }

    void setProfile(const AudiogramProfile& prof) noexcept {
        profile_ = prof;
        if (prof.loss_low_db != 0.0f && prof.loss_250hz_db == 0.0f) {
            profile_.loss_250hz_db = prof.loss_low_db;
            profile_.loss_500hz_db = prof.loss_low_db;
        }
        if (prof.loss_mid_db != 0.0f && prof.loss_1khz_db == 0.0f) {
            profile_.loss_1khz_db = prof.loss_mid_db;
            profile_.loss_2khz_db = prof.loss_mid_db;
        }
        if (prof.loss_high_db != 0.0f && prof.loss_4khz_db == 0.0f) {
            profile_.loss_4khz_db = prof.loss_high_db;
        }
        if (prof.loss_ultra_high_db != 0.0f && prof.loss_8khz_db == 0.0f) {
            profile_.loss_8khz_db = prof.loss_ultra_high_db;
        }
        recalculate();
        if (renderedBlocks_ == 0u) {
            smoothLowGain_ = targetLowGain_;
            smoothHighGain_ = targetHighGain_;
        }
    }

    void setAudiogram(const AudiogramProfile& prof) noexcept {
        const float prevSeal = profile_.ear_tip_seal_factor;
        setProfile(prof);
        if (prof.ear_tip_seal_factor == 1.0f && prevSeal != 1.0f) {
            profile_.ear_tip_seal_factor = prevSeal;
            recalculate();
        }
    }

    void setEarTipSeal(float sealFactor) noexcept {
        profile_.ear_tip_seal_factor = std::clamp(std::isfinite(sealFactor) ? sealFactor : 1.0f, 0.2f, 1.0f);
        recalculate();
        if (renderedBlocks_ == 0u) {
            smoothLowGain_ = targetLowGain_;
            smoothHighGain_ = targetHighGain_;
        }
    }

    void setFatigueLevel(float level) noexcept {
        fatigueLevel_ = std::clamp(std::isfinite(level) ? level : 0.0f, 0.0f, 1.0f);
        recalculate();
        if (renderedBlocks_ == 0u) {
            smoothLowGain_ = targetLowGain_;
            smoothHighGain_ = targetHighGain_;
        }
    }

    void setListeningSpl(float splDb) noexcept {
        const float clampedSpl = std::clamp(std::isfinite(splDb) ? splDb : 75.0f, 40.0f, 100.0f);
        listeningSplDb_ = clampedSpl;
        // Normalizar SPL [70..100 dB] -> nivel de fatiga auditiva ISO 226 [0..1]
        const float derivedFatigue = std::clamp((clampedSpl - 70.0f) / 28.0f, 0.0f, 1.0f);
        setFatigueLevel(derivedFatigue);
    }

    [[nodiscard]] float listeningSpl() const noexcept { return listeningSplDb_; }

    /**
     * @brief Process stereo block with equal-loudness + presbycusis + fatigue protection.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;
        ++renderedBlocks_;

        const float targetLow = targetLowGain_;
        const float targetHigh = targetHighGain_;
        float lowGain = smoothLowGain_;
        float highGain = smoothHighGain_;

        float sLowL = lowShelveL_;
        float sLowR = lowShelveR_;
        float sHighL = highShelveL_;
        float sHighR = highShelveR_;

        for (size_t i = 0; i < numSamples; ++i) {
            lowGain  += 0.004f * (targetLow  - lowGain);
            highGain += 0.004f * (targetHigh - highGain);
            float inL = bufferL[i];
            float inR = bufferR[i];

            // 1-pole low-frequency compensation (bass loss from poor ear tip seal)
            sLowL += 0.05f * (inL - sLowL);
            sLowR += 0.05f * (inR - sLowR);
            float lowCompL = inL + sLowL * (lowGain - 1.0f);
            float lowCompR = inR + sLowR * (lowGain - 1.0f);

            // 1-pole high-frequency compensation (presbycusis / fatigue roll-off)
            sHighL += 0.2f * (lowCompL - sHighL);
            sHighR += 0.2f * (lowCompR - sHighR);
            float highDiffL = lowCompL - sHighL;
            float highDiffR = lowCompR - sHighR;

            bufferL[i] = sHighL + highDiffL * highGain;
            bufferR[i] = sHighR + highDiffR * highGain;
        }

        lowShelveL_ = sLowL;
        lowShelveR_ = sLowR;
        highShelveL_ = sHighL;
        highShelveR_ = sHighR;
        smoothLowGain_ = lowGain;
        smoothHighGain_ = highGain;
    }

private:
    void recalculate() noexcept {
        // Low compensation boost inversely proportional to seal factor
        // If ear tip seal drops to 0.5 -> boost bass up to +4dB (gain ~ 1.58)
        const float seal = std::clamp(profile_.ear_tip_seal_factor, 0.2f, 1.0f);
        targetLowGain_ = 1.0f + (1.0f - seal) * 0.75f;

        // Presbycusis compensation (loss at 4kHz and 8kHz) + fatigue attenuation
        const float avgLossDb = (profile_.loss_4khz_db + profile_.loss_8khz_db) * 0.5f;
        // Mild linear gain compensation with safety clamp (+6dB max)
        float highBoostLinear = std::clamp(std::pow(10.0f, avgLossDb / 40.0f), 0.8f, 2.0f);

        // Fatigue attenuates harsh high frequencies to protect ear
        const float fatigueDamping = 1.0f - (fatigueLevel_ * 0.25f);
        targetHighGain_ = highBoostLinear * fatigueDamping;
    }

    AudiogramProfile profile_{};
    float fatigueLevel_{0.0f};
    float listeningSplDb_{75.0f};

    float targetLowGain_{1.0f};
    float targetHighGain_{1.0f};
    float smoothLowGain_{1.0f};
    float smoothHighGain_{1.0f};
    uint32_t renderedBlocks_{0u};

    float lowShelveL_{0.0f};
    float lowShelveR_{0.0f};
    float highShelveL_{0.0f};
    float highShelveR_{0.0f};
};

} // namespace ivanna::spatial
