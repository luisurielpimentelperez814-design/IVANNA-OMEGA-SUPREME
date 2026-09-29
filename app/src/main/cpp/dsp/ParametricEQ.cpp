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
    const bool newActive = (g > 0.02f || g < -0.02f);
    // Si los coeficientes no cambiaron (p. ej. el usuario arrastra otra banda
    // distinta), no reiniciar ni disparar crossfades innecesarios.
    if (std::fabs(bandsL[b].b0 - b0) < 1e-6f &&
        std::fabs(bandsL[b].b1 - b1) < 1e-6f &&
        std::fabs(bandsL[b].b2 - b2) < 1e-6f &&
        std::fabs(bandsL[b].a1 - a1) < 1e-6f &&
        std::fabs(bandsL[b].a2 - a2) < 1e-6f) {
        active_[b] = newActive;
        return;
    }
    // Reutilizar el estado si la banda ya estaba activa o en fundido (evita
    // click al reajustar en caliente); si estaba inactiva, arranca limpia.
    const bool wasActive = active_[b] || (fade_[b] > 0);
    const float x1L = wasActive ? bandsL[b].x1 : 0.f, x2L = wasActive ? bandsL[b].x2 : 0.f;
    const float y1L = wasActive ? bandsL[b].y1 : 0.f, y2L = wasActive ? bandsL[b].y2 : 0.f;
    const float x1R = wasActive ? bandsR[b].x1 : 0.f, x2R = wasActive ? bandsR[b].x2 : 0.f;
    const float y1R = wasActive ? bandsR[b].y1 : 0.f, y2R = wasActive ? bandsR[b].y2 : 0.f;
    // Anti-zipper: guardar el filtro saliente como copia y arrancar un
    // crossfade temporal (~15 ms) en process().
    // FIX (tronido al arrastrar sliders continuamente): antes sólo se iniciaba
    // el crossfade si fade_[b] == 0. Al mover un slider en la UI (eventos cada
    // 8-16 ms < 15 ms de fade), las actualizaciones intermedias encontraban
    // fade_[b] > 0 y machacaban bandsL[b] en seco cuando t ya iba cerca de 1.0,
    // produciendo escalones audibles en pleno arrastre. Ahora, si ya había un
    // fade en curso, consolidamos el filtro interpolado actual en prevL/prevR
    // y reiniciamos limpiamente la rampa de 15 ms desde t=0.
    if (fade_[b] > 0 && fadeLen_[b] > 0) {
        const float t = 1.0f - (float)fade_[b] / (float)fadeLen_[b];
        prevL[b].b0 = prevL[b].b0 + (bandsL[b].b0 - prevL[b].b0) * t;
        prevL[b].b1 = prevL[b].b1 + (bandsL[b].b1 - prevL[b].b1) * t;
        prevL[b].b2 = prevL[b].b2 + (bandsL[b].b2 - prevL[b].b2) * t;
        prevL[b].a1 = prevL[b].a1 + (bandsL[b].a1 - prevL[b].a1) * t;
        prevL[b].a2 = prevL[b].a2 + (bandsL[b].a2 - prevL[b].a2) * t;
        prevL[b].x1 = x1L; prevL[b].x2 = x2L; prevL[b].y1 = y1L; prevL[b].y2 = y2L;
        prevR[b].b0 = prevL[b].b0;
        prevR[b].b1 = prevL[b].b1;
        prevR[b].b2 = prevL[b].b2;
        prevR[b].a1 = prevL[b].a1;
        prevR[b].a2 = prevL[b].a2;
        prevR[b].x1 = x1R; prevR[b].x2 = x2R; prevR[b].y1 = y1R; prevR[b].y2 = y2R;
    } else {
        prevL[b] = bandsL[b];
        prevR[b] = bandsR[b];
    }
    fadeLen_[b] = (int)(sampleRate_ * 0.015f);   // 15 ms
    fade_[b] = fadeLen_[b];
    bandsL[b] = {b0,b1,b2,a1,a2,x1L,x2L,y1L,y2L};
    bandsR[b] = {b0,b1,b2,a1,a2,x1R,x2R,y1R,y2R};
    active_[b] = newActive;
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

    // ── Output-gain compensation (headroom management) ─────────────────────
    // Problema: las bandas son biquads EN SERIE (cascada), no en paralelo.
    // A frecuencias donde se solapan (ej. 5-8 kHz con high+presence), los
    // boosts se suman dB a dB — con AGUDOS +8.4 dB + PRESENCIA +x dB el
    // pico puede superar 0 dBFS por >10 dB, forzando al SafetyLimiter a
    // comprimir extremadamente → "revienta el audio" (pumping / distorsión).
    //
    // Solución: estimar el boost máximo de los grupos que pueden solaparse
    // (high shelf + high peak + presence) y el grupo de medios (mid + body),
    // y compensar reduciendo la ganancia de salida del EQ en la mitad del
    // exceso. El 50 % (en vez del 100 %) es conservador: no compensa por
    // completo (deja energía subjetiva al boost), pero asegura que el limiter
    // no tenga más de ~6 dB de trabajo en el peor caso razonable.
    // La compensación se aplica via g_params.master DESPUÉS de setParams, por
    // lo que no interfiere con la ganancia de salida ya calculada del GainStage.
    const float highGroup = std::max(0.f, p.high)            // Band 5
                          + std::max(0.f, p.high * 0.5f)     // Band 7
                          + std::max(0.f, p.presence);        // Band 6
    const float midGroup  = std::max(0.f, p.mid)             // Band 4
                          + std::max(0.f, p.mid * 0.6f);      // Band 3
    const float lowGroup  = std::max(0.f, p.low)             // Band 0
                          + std::max(0.f, p.low * 0.5f);      // Band 1
    const float maxStack  = std::max({highGroup, midGroup, lowGroup});
    const float otherSum  = std::max(0.f, (highGroup + midGroup + lowGroup) - maxStack);
    // Compensar progresivamente desde 0 dB hacia arriba (sin zona muerta de 3 dB)
    // para que cualquier boost por encima de 0 dB ajuste el headroom de GainStage
    // sin saturar etapas posteriores.
    if (maxStack > 0.0f) {
        eqOutputCompensationDb_ = maxStack * 0.75f + otherSum * 0.25f;
    } else {
        eqOutputCompensationDb_ = 0.f;
    }
}

void ParametricEQ::process(float* l,float* r,int frames) noexcept {
    if(frames<=0) return;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
    // Lista de bandas activas resuelta fuera del loop de muestras: en
    // configuracion plana (todos los faders a 0 dB) el EQ es un no-op
    // bit-exacto y la cadena queda transparente de verdad.
    // Lista de bandas a procesar: activas O con fundido anti-zipper en curso
    // (una banda recién puesta a 0 dB sigue procesando hasta terminar de
    // fundirse hacia la identidad — sin clic al apagarla).
    int idx[NUM_BANDS]; int nAct = 0;
    for(int b=0;b<NUM_BANDS;++b) if(active_[b] || fade_[b] > 0) idx[nAct++]=b;
    if(nAct==0) return;
    for(int i=0;i<frames;++i){
        float L=l[i], R=r[i];
        for(int k=0;k<nAct;++k){
            const int b=idx[k];
            float nL=bandsL[b].processSample(L), nR=bandsR[b].processSample(R);
            if (fade_[b] > 0) {
                // Crossfade lineal old->new sobre la salida de la banda.
                // prevL/prevR conservan su propio estado, así la rama vieja
                // sigue siendo el filtro original hasta desvanecerse.
                const float t = (fadeLen_[b] > 0) ? (1.0f - (float)fade_[b] / (float)fadeLen_[b]) : 1.0f;
                const float oL = prevL[b].processSample(L);
                const float oR = prevR[b].processSample(R);
                nL = oL + (nL - oL) * t;
                nR = oR + (nR - oR) * t;
                fade_[b]--;
            }
            L=nL; R=nR;
        }
        l[i]=L; r[i]=R;
    }
#pragma clang diagnostic pop
}

} // namespace ivanna
