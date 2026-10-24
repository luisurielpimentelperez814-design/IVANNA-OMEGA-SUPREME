#include "variable_asrc.hpp"
#include <algorithm>
#include <cmath>
namespace ivanna::dsp {
VariableAsrc::VariableAsrc() noexcept : VariableAsrc(Config{}) {}
VariableAsrc::VariableAsrc(const Config& c) noexcept : cfg_(c) {
 valid_=std::isfinite(c.minRatio)&&std::isfinite(c.maxRatio)&&std::isfinite(c.maxSlewPpmPerSecond)&&std::isfinite(c.outputRateHz)&&c.minRatio>0&&c.maxRatio>=c.minRatio&&c.maxSlewPpmPerSecond>=0&&c.outputRateHz>=8000;
 double r=valid_?std::clamp(1.0,c.minRatio,c.maxRatio):1.0; targetRatio_.store(r); appliedRatio_=r;
}
bool VariableAsrc::setTargetRatio(double r) noexcept {
 if(!valid_||!std::isfinite(r)||r<=0)return false;
 double b=std::clamp(r,cfg_.minRatio,cfg_.maxRatio); if(b!=r)ratioClamps_.fetch_add(1,std::memory_order_relaxed); targetRatio_.store(b,std::memory_order_release); return true;
}
float VariableAsrc::at(int64_t f,unsigned c) const noexcept { if(f<0)return 0; return ring_[(static_cast<uint64_t>(f)%kRingFrames)*2+c]; }
bool VariableAsrc::process(const float* in,size_t n,float* out,size_t cap,size_t& made) noexcept {
 made=0; if(!valid_||(!in&&n)||(!out&&cap))return false; if(!n){++insufficient_;return true;}
 double rp=std::max(0.0,readPosition_); uint64_t ri=static_cast<uint64_t>(rp);
 uint64_t backlog=writePosition_>ri?writePosition_-ri:0;
 if(n>=kRingFrames||backlog+n>=kRingFrames-2){overruns_.fetch_add(1,std::memory_order_relaxed);return false;}
 for(size_t i=0;i<n;++i)for(unsigned c=0;c<2;++c){float v=in[i*2+c];if(!std::isfinite(v)){v=0;++invalidSamples_;}ring_[((writePosition_+i)%kRingFrames)*2+c]=v;}
 writePosition_+=n; const double target=targetRatio_.load(std::memory_order_acquire),maxStep=cfg_.maxSlewPpmPerSecond*1e-6/cfg_.outputRateHz;
 while(made<cap){int64_t i=static_cast<int64_t>(std::floor(readPosition_));if(i<1||static_cast<uint64_t>(i+2)>=writePosition_)break;
 double f=readPosition_-i,f2=f*f,f3=f2*f;
 for(unsigned c=0;c<2;++c){double y0=at(i-1,c),y1=at(i,c),y2=at(i+1,c),y3=at(i+2,c);double y=.5*((2*y1)+(-y0+y2)*f+(2*y0-5*y1+4*y2-y3)*f2+(-y0+3*y1-3*y2+y3)*f3);if(!std::isfinite(y)){y=0;++invalidSamples_;}out[made*2+c]=static_cast<float>(y);}
 appliedRatio_+=std::clamp(target-appliedRatio_,-maxStep,maxStep);readPosition_+=appliedRatio_;++made;
 }
 inputFrames_+=n;outputFrames_+=made;if(!made)++insufficient_;return true;
}
VariableAsrc::Metrics VariableAsrc::metrics() const noexcept { Metrics m; m.targetRatio=targetRatio_.load();m.appliedRatio=appliedRatio_;m.inputFrames=inputFrames_;m.outputFrames=outputFrames_;m.invalidSamples=invalidSamples_;m.ratioClamps=ratioClamps_.load();m.overruns=overruns_.load();m.insufficientInputBlocks=insufficient_;return m; }
void VariableAsrc::reset() noexcept { readPosition_=static_cast<double>(writePosition_)+1.0;appliedRatio_=targetRatio_.load();inputFrames_=outputFrames_=invalidSamples_=insufficient_=0; }
}