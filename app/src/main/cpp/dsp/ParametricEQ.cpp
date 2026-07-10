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
    // p.low / p.mid / p.high / p.presence arrive as dB values directly from Kotlin
    // (DSPBridge.setParams passes them verbatim — they are NOT 0..1 scalars).
    // Previous code multiplied by 8/12 which produced wild values (e.g. +6 dB * 12 = +72 dB).
    // Fix: use dB values directly. clamp to ±18 dB for safety.
    auto clampDb = [](float db) { return db < -18.f ? -18.f : db > 18.f ? 18.f : db; };

    // Band 0: Low shelf  ~80 Hz  — driven by low param
    setBand(0, 80.f,   0.707f, clampDb(p.low));
    // Band 1: Peaking   ~200 Hz — low param (half weight for smooth shelf)
    setBand(1, 200.f,  p.resonance, clampDb(p.low * 0.5f));
    // Band 2: Peaking   ~500 Hz — mid transition (no direct param, flat)
    setBand(2, 500.f,  p.resonance, 0.f);
    // Band 3: Peaking   at freq Hz — banda paramétrica libre (freq/Q ajustables).
    //
    // FIX (tuning magistral): antes su ganancia venía de `p.master * 0.25f`
    // — es decir, el volumen final de salida (GainStage::outputGain_ =
    // dbToLin(p.master), ver GainStage.cpp) TAMBIÉN reformaba el timbre en
    // esta banda cada vez que el usuario subía/bajaba el volumen. No es una
    // curva de compensación Fletcher-Munson intencional (sería dependiente
    // de frecuencia grave/aguda, no un solo bell en freq=1kHz por defecto);
    // es un parámetro reusado por atajo. Se desacopla: sin ganancia propia
    // dedicada expuesta desde Kotlin, queda plana (igual que banda 2) hasta
    // que se cablee un control real — así el volumen deja de teñir el tono.
    setBand(3, p.freq, p.resonance, 0.f);
    // Band 4: Peaking   ~2.5 kHz — mid param
    setBand(4, 2500.f, p.resonance, clampDb(p.mid));
    // Band 5: Peaking   ~5 kHz  — high param
    setBand(5, 5000.f, p.resonance, clampDb(p.high));
    // Band 6: Peaking   ~8 kHz  — presence param
    setBand(6, 8000.f, p.resonance, clampDb(p.presence));
    // Band 7: High shelf ~12 kHz — high param (half for air)
    setBand(7, 12000.f, 0.707f, clampDb(p.high * 0.5f));
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
