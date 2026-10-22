#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace ivanna::audio {

// App-owned, allocation-free stereo software ASRC. Configure/buildKernel are
// control-thread operations; process() is intended for one dedicated audio
// thread per instance. This does not itself attach to Android's global HAL.
class OmegaSoftwareAsrc final {
public:
    static constexpr int kTaps = 64;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kPhases = 1024;
    static constexpr double kMaxClockCorrectionPpm = 500.0;

    bool configure(int inputRate, int outputRate) noexcept {
        if (inputRate < 8000 || inputRate > 384000 ||
            outputRate < 8000 || outputRate > 384000) {
            configured_ = false;
            reset();
            return false;
        }
        inputRate_ = inputRate;
        outputRate_ = outputRate;
        nominalStep_ = static_cast<double>(inputRate) / outputRate;
        correctionPpm_ = 0.0;
        step_ = nominalStep_;
        buildKernel();
        reset();
        configured_ = true;
        return true;
    }

    void reset() noexcept {
        historyLeft_.fill(0.0f);
        historyRight_.fill(0.0f);
        position_ = static_cast<double>(kTaps);
        correctionPpm_ = 0.0;
        step_ = nominalStep_;
    }

    // Positive correction asks for a slightly faster output-clock ratio.
    // The correction is bounded; invalid input fails closed to nominal ratio.
    void setClockCorrectionPpm(double ppm) noexcept {
        correctionPpm_ = std::isfinite(ppm)
            ? std::clamp(ppm, -kMaxClockCorrectionPpm, kMaxClockCorrectionPpm)
            : 0.0;
        step_ = nominalStep_ / (1.0 + correctionPpm_ * 1.0e-6);
    }

    double clockCorrectionPpm() const noexcept { return correctionPpm_; }
    double nominalStep() const noexcept { return nominalStep_; }
    bool configured() const noexcept { return configured_; }

    // Interleaved stereo float PCM. Returns produced frames. If the destination
    // is too small, returns zero without consuming input or changing state.
    // Caller must provide distinct input/output buffers and one thread/instance.
    std::size_t process(const float* input, std::size_t inputFrames,
                        float* output, std::size_t outputCapacityFrames) noexcept {
        if (!configured_ || input == nullptr || output == nullptr ||
            inputFrames == 0 || outputCapacityFrames == 0 ||
            !std::isfinite(step_) || step_ <= 0.0) return 0;

        const double estimate = (static_cast<double>(inputFrames) + 2.0) / step_ + 2.0;
        if (!std::isfinite(estimate) ||
            estimate > static_cast<double>(outputCapacityFrames)) return 0;

        // Bounded scratch storage: callers submit chunks no larger than this.
        if (inputFrames > kMaxInputFrames) return 0;
        const std::size_t total = static_cast<std::size_t>(kTaps) + inputFrames;
        std::array<float, kTaps + kMaxInputFrames> left{};
        std::array<float, kTaps + kMaxInputFrames> right{};
        std::copy(historyLeft_.begin(), historyLeft_.end(), left.begin());
        std::copy(historyRight_.begin(), historyRight_.end(), right.begin());
        for (std::size_t i = 0; i < inputFrames; ++i) {
            left[kTaps + i] = input[2 * i];
            right[kTaps + i] = input[2 * i + 1];
        }

        std::size_t produced = 0;
        double p = position_;
        while (p < static_cast<double>(total)) {
            const auto base = static_cast<std::size_t>(p);
            if (base + kHalf > total - 1) break;
            const double phasePosition = (p - static_cast<double>(base)) * kPhases;
            const int phase = std::min(static_cast<int>(phasePosition), kPhases - 1);
            const float alpha = static_cast<float>(phasePosition - phase);
            const float* c0 = kernel_.data() + static_cast<std::size_t>(phase) * kTaps;
            const float* c1 = c0 + kTaps;
            const std::size_t start = base - kHalf + 1;
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < kTaps; ++k) {
                const float c = c0[k] * (1.0f - alpha) + c1[k] * alpha;
                l += c * left[start + static_cast<std::size_t>(k)];
                r += c * right[start + static_cast<std::size_t>(k)];
            }
            output[2 * produced] = l;
            output[2 * produced + 1] = r;
            ++produced;
            p += step_;
        }

        std::copy(left.begin() + static_cast<std::ptrdiff_t>(inputFrames),
                  left.begin() + static_cast<std::ptrdiff_t>(inputFrames + kTaps),
                  historyLeft_.begin());
        std::copy(right.begin() + static_cast<std::ptrdiff_t>(inputFrames),
                  right.begin() + static_cast<std::ptrdiff_t>(inputFrames + kTaps),
                  historyRight_.begin());
        position_ = p - static_cast<double>(inputFrames);
        return produced;
    }

    static constexpr std::size_t kMaxInputFrames = 8192;

private:
    void buildKernel() noexcept {
        constexpr double pi = 3.1415926535897932384626433832795;
        const double cutoff = 0.94 * std::min(
            static_cast<double>(outputRate_) / inputRate_, 1.0);
        for (int phase = 0; phase <= kPhases; ++phase) {
            const double frac = static_cast<double>(phase) / kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k) {
                const double x = static_cast<double>(k - kHalf + 1) - frac;
                const double sinc = std::abs(x) < 1.0e-12
                    ? cutoff : std::sin(pi * cutoff * x) / (pi * x);
                const double u = x / kHalf;
                const double window = std::abs(u) >= 1.0 ? 0.0 :
                    0.35875 + 0.48829 * std::cos(pi * u) +
                    0.14128 * std::cos(2.0 * pi * u) +
                    0.01168 * std::cos(3.0 * pi * u);
                const float value = static_cast<float>(sinc * window);
                kernel_[static_cast<std::size_t>(phase) * kTaps + k] = value;
                sum += value;
            }
            if (std::abs(sum) > 1.0e-15) {
                for (int k = 0; k < kTaps; ++k)
                    kernel_[static_cast<std::size_t>(phase) * kTaps + k] =
                        static_cast<float>(kernel_[static_cast<std::size_t>(phase) * kTaps + k] / sum);
            } else {
                for (int k = 0; k < kTaps; ++k)
                    kernel_[static_cast<std::size_t>(phase) * kTaps + k] = 0.0f;
            }
        }
    }

    std::array<float, static_cast<std::size_t>(kPhases + 1) * kTaps> kernel_{};
    std::array<float, kTaps> historyLeft_{};
    std::array<float, kTaps> historyRight_{};
    int inputRate_ = 0;
    int outputRate_ = 0;
    double nominalStep_ = 1.0;
    double step_ = 1.0;
    double correctionPpm_ = 0.0;
    double position_ = static_cast<double>(kTaps);
    bool configured_ = false;
};

} // namespace ivanna::audio
