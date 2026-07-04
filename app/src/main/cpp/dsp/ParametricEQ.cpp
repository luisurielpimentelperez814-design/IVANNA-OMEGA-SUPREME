#include "../include/ParametricEQ.h"
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IVANNA_EQ_NEON 1
#else
#define IVANNA_EQ_NEON 0
#endif

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
    // Band 3: FIX audio-cleanup: ya no usa p.master (evita doble ganancia con GainStage)
    setBand(3, p.freq, p.resonance, 0.f);
    // Band 4: Peaking   ~2.5 kHz — mid param
    setBand(4, 2500.f, p.resonance, clampDb(p.mid));
    // FIX audio-cleanup (sibilancia): bandas 5/6/7 desacopladas para no sumar boost en 5-12kHz
    setBand(5, 5000.f, p.resonance, 0.f);
    setBand(6, 7500.f, 2.2f, clampDb(p.presence * 0.7f));
    setBand(7, 12000.f, 0.707f, clampDb(p.high * 0.6f));
}

void ParametricEQ::process(float* l,float* r,int frames) noexcept {
    if(frames<=0) return;
#if IVANNA_EQ_NEON
    // OPTIMIZACION NEON: bandsL[b] y bandsR[b] comparten SIEMPRE los mismos
    // coeficientes (ver setBand(): "bandsR[b] = bandsL[b];"). Antes se
    // recorrian 8 biquads para L y 8 biquads para R en dos pasadas
    // escalares separadas -> mismo trabajo aritmetico duplicado. Aqui se
    // empaqueta (L,R) en un float32x2_t y se corre la cascada UNA vez;
    // el estado (x1,x2,y1,y2) sigue siendo independiente por canal.
    // Estructura, orden de bandas y matematica DF-I sin cambios.
    for (int i = 0; i < frames; ++i) {
        float32x2_t v = {l[i], r[i]};
        for (int b = 0; b < NUM_BANDS; ++b) {
            Biquad& L = bandsL[b];
            Biquad& R = bandsR[b];
            const float32x2_t b0 = vdup_n_f32(L.b0);
            const float32x2_t b1 = vdup_n_f32(L.b1);
            const float32x2_t b2 = vdup_n_f32(L.b2);
            const float32x2_t na1 = vdup_n_f32(-L.a1);
            const float32x2_t a2  = vdup_n_f32(L.a2);
            const float32x2_t x1 = {L.x1, R.x1};
            const float32x2_t x2 = {L.x2, R.x2};
            const float32x2_t y1 = {L.y1, R.y1};
            const float32x2_t y2 = {L.y2, R.y2};

            // y = b0*v + b1*x1 + b2*x2 - a1*y1 - a2*y2
            float32x2_t y = vmla_f32(vmla_f32(vmla_f32(vmul_f32(b0, v), b1, x1), b2, x2), na1, y1);
            y = vmls_f32(y, a2, y2);

            L.x2 = L.x1; L.x1 = vget_lane_f32(v, 0);
            R.x2 = R.x1; R.x1 = vget_lane_f32(v, 1);
            L.y2 = L.y1; L.y1 = vget_lane_f32(y, 0);
            R.y2 = R.y1; R.y1 = vget_lane_f32(y, 1);
            v = y;
        }
        l[i] = vget_lane_f32(v, 0);
        r[i] = vget_lane_f32(v, 1);
    }
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    for(int i=0;i<frames;++i){
        float L=l[i], R=r[i];
        for(int b=0;b<NUM_BANDS;++b){ L=bandsL[b].processSample(L); R=bandsR[b].processSample(R); }
        l[i]=L; r[i]=R;
    }
#pragma clang diagnostic pop
#endif
}

} // namespace ivanna
