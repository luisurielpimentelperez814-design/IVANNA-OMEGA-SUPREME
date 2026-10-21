#pragma once
#include "audio_hardware_clock.hpp"
#include <algorithm>
#include <cmath>
namespace ivanna::audio {
// Convert measured output-clock drift into a bounded command for the HAL's
// existing variable-rate ASRC. Instantiate beside the HAL output stream;
// AudioFlinger insert effects do not own the physical output queue.
class HardwareClockRateServo {
public:
    struct Output { bool enabled=false; double inputFramesPerOutputFrame=1.0; double correctionPpm=0.0; };
    static constexpr double kMaxCorrectionPpm=500.0;
    static constexpr double kMaxSlewPpmPerUpdate=25.0;
    static constexpr unsigned kLockSamples=3;
    Output observe(const HardwareClockStatus& status) noexcept {
        if (!status.valid || !std::isfinite(status.driftPpm) ||
            !std::isfinite(status.measuredSampleRateHz) || status.measuredSampleRateHz<=0.0 ||
            std::abs(status.driftPpm)>=5000.0) { reset(); return {}; }
        if (validSamples_<kLockSamples) ++validSamples_;
        const double target=std::clamp(status.driftPpm,-kMaxCorrectionPpm,kMaxCorrectionPpm);
        const double filtered=correctionPpm_+0.2*(target-correctionPpm_);
        correctionPpm_+=std::clamp(filtered-correctionPpm_,-kMaxSlewPpmPerUpdate,kMaxSlewPpmPerUpdate);
        locked_=validSamples_>=kLockSamples;
        return output();
    }
    Output current() const noexcept { return output(); }
    void reset() noexcept { validSamples_=0; correctionPpm_=0.0; locked_=false; }
private:
    Output output() const noexcept {
        if (!locked_) return {};
        // Fast output clock => fewer source frames per output frame.
        const double ratio=1.0/(1.0+correctionPpm_*1.0e-6);
        if (!std::isfinite(ratio) || ratio<0.9995 || ratio>1.0005) return {};
        return {true,ratio,correctionPpm_};
    }
    unsigned validSamples_=0;
    double correctionPpm_=0.0;
    bool locked_=false;
};
} // namespace ivanna::audio
