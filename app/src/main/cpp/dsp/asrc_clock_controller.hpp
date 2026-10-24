#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace ivanna::dsp {
enum class AsrcState { INIT,WAITING_FOR_SAMPLES,TRACKING,HOLD,BYPASS,FAULT };
struct ClockSample { uint64_t presentedFrames=0; int64_t timestampNs=0; uint32_t nominalRateHz=0; };
struct AsrcControlSnapshot { AsrcState state=AsrcState::INIT;double driftPpm=0,targetPpm=0,appliedRatio=1;const char* reason="not-initialized"; };
class AsrcClockController final {
public:
 explicit AsrcClockController(unsigned n=4) noexcept:required_(n<2?2:n){}
 AsrcControlSnapshot update(const ClockSample&s,bool routeValidated,bool enabled,AsrcState ext=AsrcState::TRACKING) noexcept {
  if(!enabled)return set(AsrcState::BYPASS,"feature-disabled");
  if(ext==AsrcState::HOLD||ext==AsrcState::BYPASS||ext==AsrcState::FAULT)return set(ext,"external-safe-state");
  if(!routeValidated)return set(AsrcState::WAITING_FOR_SAMPLES,"BLOCKED:no-writable-correlated-route");
  if(s.timestampNs<=0||s.nominalRateHz<8000||s.nominalRateHz>768000)return set(AsrcState::FAULT,"invalid-timestamp-or-rate");
  if(!have_){prev_=s;have_=true;return set(AsrcState::WAITING_FOR_SAMPLES,"first-anchor");}
  if(s.timestampNs<=prev_.timestampNs||s.presentedFrames<prev_.presentedFrames)return set(AsrcState::HOLD,"timestamp-or-frame-regressed");
  int64_t dt=s.timestampNs-prev_.timestampNs;if(dt<1000000LL||dt>1000000000LL)return set(AsrcState::HOLD,"impossible-delta-time");
  uint64_t df=s.presentedFrames-prev_.presentedFrames;double ppm=(static_cast<double>(df)*1e9/dt/s.nominalRateHz-1)*1e6;
  if(!std::isfinite(ppm)||std::abs(ppm)>2000)return set(AsrcState::HOLD,"drift-outside-safe-range");
  prev_=s;if(pairs_&&std::abs(ppm-drift_)>250)return set(AsrcState::HOLD,"drift-outlier");
  drift_=pairs_?0.85*drift_+0.15*ppm:ppm;if(++pairs_<required_)return set(AsrcState::WAITING_FOR_SAMPLES,"collecting-correlated-pairs");
  target_=std::clamp(-drift_,-500.0,500.0);return set(AsrcState::TRACKING,"correlated-route-valid");
 }
 void reset() noexcept {have_=false;pairs_=0;drift_=target_=0;}
private:
 AsrcControlSnapshot set(AsrcState s,const char*r) noexcept {snap_.state=s;snap_.reason=r;snap_.driftPpm=drift_;snap_.targetPpm=s==AsrcState::TRACKING?target_:0;snap_.appliedRatio=1+snap_.targetPpm*1e-6;return snap_;}
 unsigned required_=4,pairs_=0;bool have_=false;ClockSample prev_{};double drift_=0,target_=0;AsrcControlSnapshot snap_{};
};
}