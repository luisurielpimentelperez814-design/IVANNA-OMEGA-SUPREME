#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

// Sony HCD-PZ1D / MHC-PZ1D experimental lab profile.
// Source: user-supplied laboratory measurements, 2026-11-06.
// Runtime PCM profile for the live AudioEffect path; it models the measured output signature,
// but cannot electrically command the external Sony amplifier's supply rails.
namespace ivanna { namespace dsp { namespace sony_hcd_pz1d {

constexpr char kProfileId[] = "SONY_HCD_PZ1D";
constexpr char kMeasurementProvenance[] = "user_lab_measurements_2026-11-06";
constexpr float kTestFrequencyHz = 1000.0f;
constexpr float kTestLoadOhms = 6.0f;
constexpr float kTestPowerWatts = 50.0f;

constexpr float kHighRailPositiveV = 58.2f;
constexpr float kHighRailNegativeV = -58.2f;
constexpr float kHighRailNoLoadMagnitudeV = 62.1f;
constexpr float kHighRailFullPowerMagnitudeV = 52.4f;
constexpr float kLowRailPositiveV = 31.5f;
constexpr float kLowRailNegativeV = -31.5f;
constexpr float kClassHSwitchThresholdPeakV = 24.5f;
// Calibration anchor: normalized PCM peak 1.0 represents the reported 50 W / 6 ohm test point.
constexpr float kPcmPeakReferenceVoltageV = 24.5f;
constexpr float kClassHGuardBandV = 7.0f;
constexpr uint32_t kClassHTurnOnNs = 850u;
constexpr float kClassHReleaseMinMs = 4.5f;
constexpr float kClassHReleaseMaxMs = 12.0f;

constexpr float kH2MinDbc = -72.0f;
constexpr float kH2MaxDbc = -68.0f;
constexpr float kH2NominalDbc = -70.0f;
constexpr float kH3BelowThresholdDbc = -78.0f;
constexpr float kH3DuringTransitionDbc = -62.0f;

constexpr float kProtectionPeakCurrentA = 7.5f;
constexpr float kProtectionPeakTripMaxMs = 15.0f;
constexpr float kProtectionSustainedCurrentMinA = 4.5f;
constexpr float kProtectionSustainedCurrentMaxA = 5.5f;
constexpr float kProtectionSustainedTripMs = 1200.0f;
constexpr float kDcDetectResistanceOhms = 47000.0f;
constexpr float kDcDetectCapacitanceF = 47.0e-6f;
constexpr float kDcDetectCutoffHz = 1.0f / (2.0f * 3.14159265358979323846f * kDcDetectResistanceOhms * kDcDetectCapacitanceF);
constexpr float kDcOffsetTripV = 1.5f;
constexpr float kDcOffsetTripTimeMs = 340.0f;

// Polynomial calibration convention: x_peak=1 at 50 W / 6 ohm.
// For y=x+a2*x^2+a3*x^3 and a sine, a2=2*H2 and a3=4*H3.
constexpr float kA2Min = 2.0f * 0.00025118864f; // H2 at -72 dBc
constexpr float kA2Max = 2.0f * 0.00039810717f; // H2 at -68 dBc
constexpr float kA2Nominal = 2.0f * 0.00031622777f; // H2 at -70 dBc
constexpr float kA3BelowThreshold = 4.0f * 0.00012589254f; // H3 at -78 dBc
constexpr float kA3DuringTransition = 4.0f * 0.00079432823f; // H3 at -62 dBc

struct HarmonicOutput { float sample; bool class_h_high_rail; };

// Discrete approximation of the reported Class-H envelope. Turn-on is
// represented at the next simulation sample (850 ns is sub-sample at
// common audio rates); release uses the conservative 12 ms end of range.
class ClassHState {
public:
    bool update(float absOutputPeakVolts, uint32_t sampleRate) noexcept {
        if (sampleRate == 0u || !std::isfinite(absOutputPeakVolts)) return high_;
        const uint32_t releaseSamples = static_cast<uint32_t>(
            (kClassHReleaseMaxMs * 0.001f) * static_cast<float>(sampleRate));
        if (absOutputPeakVolts >= kClassHSwitchThresholdPeakV) {
            high_ = true;
            belowThresholdSamples_ = 0u;
        } else if (high_) {
            if (belowThresholdSamples_ < releaseSamples) ++belowThresholdSamples_;
            if (belowThresholdSamples_ >= releaseSamples) {
                high_ = false;
                belowThresholdSamples_ = 0u;
            }
        }
        return high_;
    }
    void reset() noexcept { high_ = false; belowThresholdSamples_ = 0u; }
    bool highRailSelected() const noexcept { return high_; }
private:
    bool high_ = false;
    uint32_t belowThresholdSamples_ = 0u;
};

// Apply only the measured H3 component after the Volterra H2 kernel.
inline float applyH3(float x, bool classHHighRail, float wet = 1.0f) noexcept {
    if (!std::isfinite(x) || !std::isfinite(wet)) return 0.0f;
    const float a3 = classHHighRail ? kA3DuringTransition : kA3BelowThreshold;
    const float amount = std::clamp(wet, 0.0f, 1.0f);
    const float y = x + amount * a3 * x * x * x;
    return std::isfinite(y) ? y : x;
}

// Memoryless harmonic reference model for tests and offline comparisons.
// Kept separate from VolterraH2Symmetric: this is not an H2/H3 memory kernel.
inline HarmonicOutput simulateMeasuredHarmonics(float x, bool classHHighRail) noexcept {
    if (!std::isfinite(x)) return {0.0f, classHHighRail};
    const float a3 = classHHighRail ? kA3DuringTransition : kA3BelowThreshold;
    const float y = x + kA2Nominal * x * x + a3 * x * x * x;
    return {std::isfinite(y) ? y : x, classHHighRail};
}

}}} // namespace ivanna::dsp::sony_hcd_pz1d
