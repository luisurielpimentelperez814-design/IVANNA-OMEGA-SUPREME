#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ivanna::dsp {

enum class AsrcState { INIT, WAITING_FOR_SAMPLES, TRACKING, HOLD, BYPASS, FAULT };

struct ClockSample {
    uint64_t presentedFrames = 0;
    int64_t timestampNs = 0;
    uint32_t nominalRateHz = 0;
};

struct AsrcControlSnapshot {
    AsrcState state = AsrcState::INIT;
    double driftPpm = 0.0;
    double targetPpm = 0.0;
    // Ratio convention: input frames consumed per output frame.
    // >1 consumes input faster and therefore emits fewer output frames.
    double appliedRatio = 1.0;
    const char* reason = "not-initialized";
};

class AsrcClockController final {
public:
    struct Config {
        unsigned requiredIntervals = 4;
        double maxCorrectionPpm = 500.0;
        double rejectDriftPpm = 2000.0;
        double maxIntervalOutlierPpm = 250.0;
        double deadbandPpm = 1.0;
        double maxSlewPpmPerSecond = 50.0;
    };

    explicit AsrcClockController(unsigned requiredIntervals = 4) noexcept
        : AsrcClockController(Config{requiredIntervals < 2 ? 2U : requiredIntervals}) {}

    explicit AsrcClockController(const Config& config) noexcept : cfg_(config) {
        validConfig_ = cfg_.requiredIntervals >= 2 &&
            std::isfinite(cfg_.maxCorrectionPpm) && cfg_.maxCorrectionPpm > 0.0 &&
            std::isfinite(cfg_.rejectDriftPpm) && cfg_.rejectDriftPpm > cfg_.maxCorrectionPpm &&
            std::isfinite(cfg_.maxIntervalOutlierPpm) && cfg_.maxIntervalOutlierPpm > 0.0 &&
            std::isfinite(cfg_.deadbandPpm) && cfg_.deadbandPpm >= 0.0 &&
            std::isfinite(cfg_.maxSlewPpmPerSecond) && cfg_.maxSlewPpmPerSecond >= 0.0;
        if (!validConfig_) snap_ = makeSnapshot(AsrcState::FAULT, "invalid-controller-config");
    }

    AsrcControlSnapshot update(const ClockSample& sample, bool routeValidated,
                               bool enabled, AsrcState externalState = AsrcState::TRACKING) noexcept {
        if (!enabled) return failClosed(AsrcState::BYPASS, "feature-disabled");
        if (externalState == AsrcState::HOLD || externalState == AsrcState::BYPASS ||
            externalState == AsrcState::FAULT)
            return failClosed(externalState, "external-safe-state");
        if (!routeValidated)
            return failClosed(AsrcState::WAITING_FOR_SAMPLES,
                              "BLOCKED:no-writable-correlated-route");
        if (!validConfig_)
            return failClosed(AsrcState::FAULT, "invalid-controller-config");
        if (sample.timestampNs <= 0 || sample.nominalRateHz < 8000 ||
            sample.nominalRateHz > 768000)
            return failClosed(AsrcState::FAULT, "invalid-timestamp-or-rate");

        if (!haveSamples_ || sample.nominalRateHz != nominalRateHz_) {
            seed(sample);
            return setWaiting("first-anchor-or-rate-change");
        }
        if (sample.timestampNs <= previous_.timestampNs ||
            sample.presentedFrames < previous_.presentedFrames) {
            clearWindow();
            seed(sample);
            return failClosed(AsrcState::HOLD, "timestamp-or-frame-regressed");
        }

        const int64_t dtNs = sample.timestampNs - previous_.timestampNs;
        if (dtNs < kMinIntervalNs || dtNs > kMaxIntervalNs) {
            clearWindow();
            seed(sample);
            return setWaiting("interval-outside-observation-window");
        }

        const uint64_t df = sample.presentedFrames - previous_.presentedFrames;
        const double instantaneousPpm =
            ((static_cast<double>(df) * 1.0e9 / static_cast<double>(dtNs)) /
             static_cast<double>(sample.nominalRateHz) - 1.0) * 1.0e6;
        if (!std::isfinite(instantaneousPpm) ||
            std::abs(instantaneousPpm) > cfg_.rejectDriftPpm) {
            clearWindow();
            seed(sample);
            return failClosed(AsrcState::HOLD, "drift-outside-safe-range");
        }
        if (sampleCount_ >= cfg_.requiredIntervals + 1 &&
            std::abs(instantaneousPpm - driftPpm_) >
                                     cfg_.maxIntervalOutlierPpm) {
            clearWindow();
            seed(sample);
            return failClosed(AsrcState::HOLD, "drift-outlier");
        }

        previous_ = sample;
        append(sample);
        if (sampleCount_ < cfg_.requiredIntervals + 1)
            return setWaiting("collecting-correlated-pairs");

        if (!estimateRegression(driftPpm_))
            return failClosed(AsrcState::HOLD, "regression-not-identifiable");
        if (!std::isfinite(driftPpm_) || std::abs(driftPpm_) > cfg_.rejectDriftPpm) {
            clearWindow();
            seed(sample);
            return failClosed(AsrcState::HOLD, "regressed-drift-outside-safe-range");
        }

        targetPpm_ = std::abs(driftPpm_) <= cfg_.deadbandPpm
                         ? 0.0
                         : std::clamp(driftPpm_, -cfg_.maxCorrectionPpm, cfg_.maxCorrectionPpm);
        const double elapsedSeconds = static_cast<double>(dtNs) * 1.0e-9;
        const double maxStep = cfg_.maxSlewPpmPerSecond * elapsedSeconds;
        appliedPpm_ += std::clamp(targetPpm_ - appliedPpm_, -maxStep, maxStep);
        appliedPpm_ = std::clamp(appliedPpm_, -cfg_.maxCorrectionPpm, cfg_.maxCorrectionPpm);
        snap_ = makeSnapshot(AsrcState::TRACKING, "correlated-route-valid");
        return snap_;
    }

    void reset() noexcept {
        clearWindow();
        previous_ = {};
        nominalRateHz_ = 0;
        targetPpm_ = driftPpm_ = appliedPpm_ = 0.0;
        snap_ = makeSnapshot(AsrcState::INIT, "reset");
    }

private:
    static constexpr int64_t kMinIntervalNs = 1'000'000LL;
    static constexpr int64_t kMaxIntervalNs = 1'000'000'000LL;
    static constexpr std::size_t kWindowCapacity = 9;

    AsrcControlSnapshot makeSnapshot(AsrcState state, const char* reason) const noexcept {
        AsrcControlSnapshot out{};
        out.state = state;
        out.reason = reason;
        out.driftPpm = std::isfinite(driftPpm_) ? driftPpm_ : 0.0;
        out.targetPpm = state == AsrcState::TRACKING ? targetPpm_ : 0.0;
        out.appliedRatio = state == AsrcState::TRACKING ? 1.0 + appliedPpm_ * 1.0e-6 : 1.0;
        return out;
    }

    AsrcControlSnapshot failClosed(AsrcState state, const char* reason) noexcept {
        clearWindow();
        previous_ = {};
        nominalRateHz_ = 0;
        snap_ = makeSnapshot(state, reason);
        return snap_;
    }

    AsrcControlSnapshot setWaiting(const char* reason) noexcept {
        targetPpm_ = 0.0;
        appliedPpm_ = 0.0;
        snap_ = makeSnapshot(AsrcState::WAITING_FOR_SAMPLES, reason);
        return snap_;
    }

    void seed(const ClockSample& sample) noexcept {
        previous_ = sample;
        nominalRateHz_ = sample.nominalRateHz;
        haveSamples_ = true;
        sampleCount_ = 0;
        samples_[0] = sample;
        sampleCount_ = 1;
        driftPpm_ = targetPpm_ = appliedPpm_ = 0.0;
    }

    void clearWindow() noexcept {
        haveSamples_ = false;
        sampleCount_ = 0;
        driftPpm_ = targetPpm_ = appliedPpm_ = 0.0;
    }

    void append(const ClockSample& sample) noexcept {
        if (sampleCount_ == kWindowCapacity) {
            for (std::size_t i = 1; i < sampleCount_; ++i) samples_[i - 1] = samples_[i];
            --sampleCount_;
        }
        samples_[sampleCount_++] = sample;
    }

    bool estimateRegression(double& outPpm) const noexcept {
        if (sampleCount_ < 3 || nominalRateHz_ == 0) return false;
        const double t0 = static_cast<double>(samples_[0].timestampNs);
        const double f0 = static_cast<double>(samples_[0].presentedFrames);
        double meanT = 0.0, meanF = 0.0;
        for (std::size_t i = 0; i < sampleCount_; ++i) {
            meanT += (static_cast<double>(samples_[i].timestampNs) - t0) * 1.0e-9;
            meanF += static_cast<double>(samples_[i].presentedFrames) - f0;
        }
        meanT /= static_cast<double>(sampleCount_);
        meanF /= static_cast<double>(sampleCount_);
        double covariance = 0.0, varianceT = 0.0;
        for (std::size_t i = 0; i < sampleCount_; ++i) {
            const double t = (static_cast<double>(samples_[i].timestampNs) - t0) * 1.0e-9;
            const double f = static_cast<double>(samples_[i].presentedFrames) - f0;
            covariance += (t - meanT) * (f - meanF);
            varianceT += (t - meanT) * (t - meanT);
        }
        if (!std::isfinite(varianceT) || varianceT <= 1.0e-12) return false;
        const double measuredRate = covariance / varianceT;
        outPpm = (measuredRate / static_cast<double>(nominalRateHz_) - 1.0) * 1.0e6;
        return std::isfinite(outPpm) && measuredRate > 0.0;
    }

    Config cfg_{};
    bool validConfig_ = false;
    bool haveSamples_ = false;
    std::size_t sampleCount_ = 0;
    uint32_t nominalRateHz_ = 0;
    ClockSample previous_{};
    std::array<ClockSample, kWindowCapacity> samples_{};
    double driftPpm_ = 0.0;
    double targetPpm_ = 0.0;
    double appliedPpm_ = 0.0;
    AsrcControlSnapshot snap_{};
};

} // namespace ivanna::dsp
