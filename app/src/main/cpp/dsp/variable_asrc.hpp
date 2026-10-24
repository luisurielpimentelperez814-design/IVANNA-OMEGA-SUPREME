#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace ivanna::dsp {
class VariableAsrc final {
public:
 static constexpr size_t kRingFrames=8192;
 struct Config { double minRatio=0.9990,maxRatio=1.0010,maxSlewPpmPerSecond=50.0,outputRateHz=48000.0; };
 struct Metrics { double targetRatio=1.0,appliedRatio=1.0; uint64_t inputFrames=0,outputFrames=0,invalidSamples=0,ratioClamps=0,overruns=0,insufficientInputBlocks=0; };
 VariableAsrc() noexcept; explicit VariableAsrc(const Config&) noexcept;
 bool valid() const noexcept { return valid_; }
 bool setTargetRatio(double) noexcept;
 bool process(const float*,size_t,float*,size_t,size_t&) noexcept;
 Metrics metrics() const noexcept; void reset() noexcept;
private:
 float at(int64_t,unsigned) const noexcept;
 Config cfg_{}; bool valid_=false; std::atomic<double> targetRatio_{1.0};
 std::atomic<uint64_t> ratioClamps_{0},overruns_{0};
 double appliedRatio_=1.0,readPosition_=1.0; uint64_t writePosition_=0;
 uint64_t inputFrames_=0,outputFrames_=0,invalidSamples_=0,insufficient_=0;
 float ring_[kRingFrames*2]{};
};
}