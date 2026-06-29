#include "../include/ParametricEQ.h"
#include <cmath>

namespace ivanna {

ParametricEQ::ParametricEQ() noexcept { reset(); }
void ParametricEQ::reset() noexcept { for(int i=0;i<NUM_BANDS;++i){ bandsL[i].reset(); bandsR[i].reset(); } }
void ParametricEQ::setSampleRate(float sr) noexcept { sampleRate_ = sr; }

void ParametricEQ::setBand(int b,float f,float q,float g) noexcept {
    if(b<0||b>=NUM_BANDS) return;
    float A = powf(10.0f, g/40.0f);
    float w0 = 2.0f * float(M_PI) * f / sampleRate_;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s/(2.0f*q);
    float b0 = 1.0f + alpha*A, b1 = -2.0f*c, b2 = 1.0f - alpha*A;
    float a0 = 1.0f + alpha/A, a1 = -2.0f*c, a2 = 1.0f - alpha/A;
    b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0;
    bandsL[b] = {b0,b1,b2,a1,a2,0,0,0,0};
    bandsR[b] = bandsL[b];
}

void ParametricEQ::setParams(const DSPParams& p) noexcept {
    // Map DSPParams to 8 parametric bands
    // Band 0: Low shelf  ~80 Hz
    // Band 1: Peaking    ~200 Hz (low param)
    // Band 2: Peaking    ~500 Hz
    // Band 3: Peaking    ~1 kHz (freq param)
    // Band 4: Peaking    ~2.5 kHz (mid param)
    // Band 5: Peaking    ~5 kHz (high param)
    // Band 6: Peaking    ~8 kHz (presence param)
    // Band 7: High shelf ~12 kHz
    setBand(0, 80.f, 0.707f, p.low * 12.f);
    setBand(1, 200.f, p.resonance, p.low * 8.f);
    setBand(2, 500.f, p.resonance, 0.f);
    setBand(3, p.freq, p.resonance, 0.f);
    setBand(4, 2500.f, p.resonance, p.mid * 8.f);
    setBand(5, 5000.f, p.resonance, p.high * 8.f);
    setBand(6, 8000.f, p.resonance, p.presence * 8.f);
    setBand(7, 12000.f, 0.707f, p.high * 6.f);
}

void ParametricEQ::process(float* l,float* r,int frames) noexcept {
    if(frames<=0) return;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for(int i=0;i<frames;++i){
        float L=l[i], R=r[i];
        for(int b=0;b<NUM_BANDS;++b){ L=bandsL[b].processSample(L); R=bandsR[b].processSample(R); }
        l[i]=L; r[i]=R;
    }
#pragma clang diagnostic pop
}

} // namespace ivanna
