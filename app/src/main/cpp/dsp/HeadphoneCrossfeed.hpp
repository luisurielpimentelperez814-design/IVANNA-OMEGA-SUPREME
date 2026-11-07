#pragma once
// HeadphoneCrossfeed — mezcla de graves entre oidos con retardo interaural (ITD) para audifonos/DAC.
//
// Problema: el estereo grabado para altavoces, escuchado con audifonos, tiene separacion interaural
// infinita en graves (cada oido oye SOLO su canal). El cerebro lo interpreta como "dentro de la
// cabeza" y cansa. En altavoces cada oido tambien oye el canal opuesto ~0,25 ms despues y filtrado
// por la cabeza (pasa-bajos): eso es lo que se restituye aqui.
//
// Formulacion (sin normalizacion global, sin tocar agudos ni el centro):
//   Ld = LP(retardo(L)), Rd = LP(retardo(R))        (LP one-pole ~700 Hz, retardo ITD)
//   outL = L + g * (Rd - Ld)
//   outR = R + g * (Ld - Rd)
// * Con L == R:  Rd == Ld  => out == in EXACTO (centro/mono intactos, sin cambio de nivel).
// * Con un solo canal activo, el opuesto recibe g*LP(señal) retardada (cue ITD + sombra de cabeza).
// * Por encima de ~700 Hz LP -> 0: los agudos de cada canal pasan sin modificar.
// * g = 0 (strength 0): bypass bit-exacto. g maximo 1/3; |out| <= |in| + g*(|Rd|+|Ld|).
// Tiempo real: sin asignaciones, sin locks, estado fijo (anillo de 128 muestras por canal).
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ivanna {

class HeadphoneCrossfeed {
public:
    void setSampleRate(float sr) noexcept {
        sr_ = sr > 8000.0f ? sr : 48000.0f;
        delaySamples_ = kItdSeconds * sr_;                       // fraccional
        lpCoef_ = 1.0f - std::exp(-2.0f * 3.14159265358979f * kCutoffHz / sr_);
        reset();
    }
    // strength 0..1 -> g 0..1/3. Fuera de rango o no finito => 0 (bypass).
    // Seguro desde el hilo de UI mientras el de audio procesa (atomico relajado).
    void setStrength(float s) noexcept {
        if (!(s > 0.0f)) { g_.store(0.0f, std::memory_order_relaxed); return; }
        g_.store((s > 1.0f ? 1.0f : s) * kMaxG, std::memory_order_relaxed);
    }
    float gain() const noexcept { return g_.load(std::memory_order_relaxed); }
    void reset() noexcept {
        for (int i = 0; i < kRing; ++i) ringL_[i] = ringR_[i] = 0.0f;
        pos_ = 0; lpL_ = lpR_ = 0.0f;
    }

    void process(float* l, float* r, int n) noexcept {
        const float g = g_.load(std::memory_order_relaxed);
        if (g == 0.0f) return;                                   // bypass exacto
        const float d = delaySamples_;
        const int di = static_cast<int>(d);
        const float fr = d - static_cast<float>(di);
        for (int i = 0; i < n; ++i) {
            const float xl = l[i], xr = r[i];
            ringL_[pos_] = xl; ringR_[pos_] = xr;
            // lectura fraccional (interpolacion lineal) a `d` muestras atras
            const int p0 = (pos_ - di) & (kRing - 1);
            const int p1 = (pos_ - di - 1) & (kRing - 1);
            const float dl = ringL_[p0] + (ringL_[p1] - ringL_[p0]) * fr;
            const float dr = ringR_[p0] + (ringR_[p1] - ringR_[p0]) * fr;
            lpL_ += lpCoef_ * (dl - lpL_);
            lpR_ += lpCoef_ * (dr - lpR_);
            const float diff = lpR_ - lpL_;
            l[i] = xl + g * diff;
            r[i] = xr - g * diff;
            pos_ = (pos_ + 1) & (kRing - 1);
        }
    }

private:
    static constexpr int kRing = 128;                // potencia de 2; cubre el ITD hasta 384 kHz (100 muestras)
    static constexpr float kItdSeconds = 0.000261f;  // Woodworth a 30 grados (altavoces a +-30): (a/c)(sin30+30*pi/180), a=8,75 cm
    static constexpr float kCutoffHz = 700.0f;       // sombra de cabeza a partir de ~700 Hz
    static constexpr float kMaxG = 1.0f / 3.0f;       // cruce -6 dB bajo el directo en graves (con g=0,5 los graves serian mono)
    float sr_ = 48000.0f, delaySamples_ = 12.53f, lpCoef_ = 0.0f;
    std::atomic<float> g_{0.0f};
    float ringL_[kRing] = {}, ringR_[kRing] = {};
    float lpL_ = 0.0f, lpR_ = 0.0f;
    int pos_ = 0;
};

}  // namespace ivanna
