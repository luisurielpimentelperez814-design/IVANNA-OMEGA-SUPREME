#include "../include/ParametricEQ.h"
#include <cmath>

namespace ivanna {

ParametricEQ::ParametricEQ() noexcept { reset(); }
void ParametricEQ::reset() noexcept { for(int i=0;i<NUM_BANDS;++i){ bandsL[i].reset(); bandsR[i].reset(); } }
void ParametricEQ::setSampleRate(float sr) noexcept {
    // Validación: fuera de rango de audio real se ignora (mantiene los
    // coeficientes vigentes en vez de generar filtros inestables).
    if (sr >= 8000.0f && sr <= 768000.0f) sampleRate_ = sr;
}

void ParametricEQ::setBand(int b,float f,float q,float g) noexcept {
    if(b<0||b>=NUM_BANDS) return;
    // Clamp de seguridad: por encima de ~Nyquist*0.98 el biquad RBJ degenera
    // (alpha -> 0, coeficientes explotan). Igual criterio que Biquad::clampFreq.
    const float nyq = sampleRate_ * 0.5f;
    if (f < 20.0f) f = 20.0f;
    if (f > nyq - 100.0f) f = nyq - 100.0f;
    if (q < 0.1f) q = 0.1f; else if (q > 10.0f) q = 10.0f;
    float A = powf(10.0f, g/40.0f);
    float w0 = 2.0f * float(M_PI) * f / sampleRate_;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s/(2.0f*q);
    float b0 = 1.0f + alpha*A, b1 = -2.0f*c, b2 = 1.0f - alpha*A;
    float a0 = 1.0f + alpha/A, a1 = -2.0f*c, a2 = 1.0f - alpha/A;
    b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0;
    // Reutilizar el estado si la banda ya estaba activa (evita click al
    // reajustar en caliente); si estaba inactiva, arranca limpia.
    const bool wasActive = active_[b];
    const float x1L = wasActive ? bandsL[b].x1 : 0.f, x2L = wasActive ? bandsL[b].x2 : 0.f;
    const float y1L = wasActive ? bandsL[b].y1 : 0.f, y2L = wasActive ? bandsL[b].y2 : 0.f;
    const float x1R = wasActive ? bandsR[b].x1 : 0.f, x2R = wasActive ? bandsR[b].x2 : 0.f;
    const float y1R = wasActive ? bandsR[b].y1 : 0.f, y2R = wasActive ? bandsR[b].y2 : 0.f;
    bandsL[b] = {b0,b1,b2,a1,a2,x1L,x2L,y1L,y2L};
    bandsR[b] = {b0,b1,b2,a1,a2,x1R,x2R,y1R,y2R};
    // Umbral 0.02 dB: por debajo es inaudible (< 1/20 del JND de nivel) y la
    // banda se marca inactiva para saltarse por completo en process().
    active_[b] = (g > 0.02f || g < -0.02f);
}

void ParametricEQ::setParams(const DSPParams& p) noexcept {
    // FIX CRÍTICO (respuesta en frecuencia): setSampleRate() no tenía NINGÚN
    // llamador en todo el proyecto y sampleRate_ se quedaba en su default de
    // 96000 Hz. Con un stream real a 48 kHz cada w0 = 2*pi*f/96000 salía a la
    // mitad → TODAS las bandas caían una octava arriba de su frecuencia
    // nominal (el low-shelf de 80 Hz actuaba en ~160 Hz, el "aire" de 12 kHz
    // en ~24 kHz = fuera de banda audible y por tanto inerte). Ahora el EQ
    // toma el sample rate real del pipeline (g_params.sampleRate, seteado en
    // el JNI) en cada actualización de parámetros.
    // Validación: barrido de tono + FFT — el -3 dB del shelf debe coincidir
    // con la frecuencia pedida a 44.1/48/96 kHz.
    setSampleRate(static_cast<float>(p.sampleRate));

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
    // Band 3: body en freq del usuario — p.mid * 0.6 (antes siempre 0dB)
    setBand(3, p.freq, p.resonance, clampDb(p.mid * 0.6f));
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
    // Lista de bandas activas resuelta fuera del loop de muestras: en
    // configuracion plana (todos los faders a 0 dB) el EQ es un no-op
    // bit-exacto y la cadena queda transparente de verdad.
    int idx[NUM_BANDS]; int nAct = 0;
    for(int b=0;b<NUM_BANDS;++b) if(active_[b]) idx[nAct++]=b;
    if(nAct==0) return;
    for(int i=0;i<frames;++i){
        float L=l[i], R=r[i];
        for(int k=0;k<nAct;++k){ const int b=idx[k]; L=bandsL[b].processSample(L); R=bandsR[b].processSample(R); }
        l[i]=L; r[i]=R;
    }
#pragma clang diagnostic pop
}

} // namespace ivanna
