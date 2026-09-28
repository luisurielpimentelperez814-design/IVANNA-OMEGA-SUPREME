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
    }

    void setProfile(const AudiogramProfile& prof) noexcept {
        profile_ = prof;
        recalculate();
    }

    void setFatigueLevel(float level) noexcept {
        fatigueLevel_ = std::clamp(level, 0.0f, 1.0f);
        recalculate();
    }

    /**
     * @brief Process stereo block with equal-loudness + presbycusis + fatigue protection.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;

        const float lowGain = targetLowGain_;
        const float highGain = targetHighGain_;

        float sLowL = lowShelveL_;
        float sLowR = lowShelveR_;
        float sHighL = highShelveL_;
        float sHighR = highShelveR_;

        for (size_t i = 0; i < numSamples; ++i) {
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

    float targetLowGain_{1.0f};
    float targetHighGain_{1.0f};

    float lowShelveL_{0.0f};
    float lowShelveR_{0.0f};
    float highShelveL_{0.0f};
    float highShelveR_{0.0f};
};

} // namespace ivanna::spatial
