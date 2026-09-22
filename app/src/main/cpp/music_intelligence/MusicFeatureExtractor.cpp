#include "MusicFeatureExtractor.hpp"
#include <algorithm>

namespace ivanna { namespace ime {

bool MusicFeatureExtractor::prepare(float sampleRate, int maxBlock) noexcept {
    if (!(sampleRate > 8000.f) || maxBlock <= 0) return false;
    sr_ = sampleRate; maxBlock_ = maxBlock;
    const float dt = 1.0f / sr_;
    const float twoPi = 6.283185307179586f;
    // un polo por borde de banda: sub<120, 120<low<500, 500<mid<2000, high>2000
    aSub_  = dt / (1.0f/(twoPi*120.f)  + dt);
    aLow_  = dt / (1.0f/(twoPi*500.f)  + dt);
    aMid_  = dt / (1.0f/(twoPi*2000.f) + dt);
    aHigh_ = dt / (1.0f/(twoPi*2000.f) + dt);
    reset();
    return true;
}

void MusicFeatureExtractor::reset() noexcept {
    lpSub_=lpLow_=lpMid_=hpState_=0.f; prevEnergy_=0.f;
    sumSq_=eSub_=eLow_=eMid_=eHigh_=0.f;
    sumLR_=sumLL_=sumRR_=0.f;
    activeSamples_=0; onsets_=0; n_=0; acc_=MusicFeatures{};
}

void MusicFeatureExtractor::processBlock(const float* l, const float* r, int frames) noexcept {
    if (!l || frames <= 0) return;
    float sumSq=0,eSub=0,eLow=0,eMid=0,eHigh=0,sumLR=0,sumLL=0,sumRR=0;
    int active=0, onsets=0;
    float peak=0.f;
    for (int i=0;i<frames;++i){
        float L=sanitize(l[i]);
        float R= r? sanitize(r[i]) : L;
        float mid=0.5f*(L+R);
        float x2=mid*mid; sumSq+=x2;
        float aL=std::fabs(L), aR=std::fabs(R);
        float ap = aL>aR?aL:aR; if (ap>peak) peak=ap;
        // bandas (un polo en cascada sobre mid)
        lpSub_ += aSub_*(mid-lpSub_);   float sub=lpSub_;
        lpLow_ += aLow_*(mid-lpLow_);   float lowband=lpLow_-sub;
        lpMid_ += aMid_*(mid-lpMid_);   float midband=lpMid_-lpLow_; // 500–2000 Hz
        float highband = mid - lpMid_;  // agudos = residual sobre corte 2k
        eSub+=sub*sub; eLow+=lowband*lowband; eMid+=midband*midband; eHigh+=highband*highband;
        // correlación L/R
        sumLR+=L*R; sumLL+=L*L; sumRR+=R*R;
        if (ap > 1e-4f) active++;
        // onset por derivada de energía instantánea
        float e=x2; if (prevEnergy_>0.f && e > 2.5f*prevEnergy_ && e>1e-6f) onsets++;
        prevEnergy_=e;
    }
    n_=frames;
    const float inv = 1.0f/frames;
    float rms = std::sqrt(sumSq*inv);
    float eTot = eSub+eLow+eMid+eHigh + 1e-12f;
    float corr = (sumLL>1e-12f && sumRR>1e-12f)
               ? (sumLR/std::sqrt(sumLL*sumRR)) : 1.0f;
    if (corr< -1.f) corr=-1.f; if (corr>1.f) corr=1.f;
    acc_.rms=rms; acc_.peak=peak;
    acc_.crestDb = (rms>1e-9f)? 20.f*std::log10(peak/rms) : 0.f;
    acc_.bassRatio   = (eSub+eLow)/eTot;
    acc_.midRatio    = eMid/eTot;
    acc_.trebleRatio = eHigh/eTot;
    acc_.stereoWidth = 1.0f - corr;
    acc_.transientRate = std::fmin(1.0f, onsets*inv*8.f);
    acc_.density = active*inv;
}

}} // namespace ivanna::ime
