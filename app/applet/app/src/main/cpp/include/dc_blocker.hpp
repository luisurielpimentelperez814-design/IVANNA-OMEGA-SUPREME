// © 2026 Luis Uriel Pimentel Pérez — GORE TNS.
#pragma once
// dc_blocker.hpp — anti-ruido digital en el borde de la cadena DSP.
//
// Ruido digital que elimina:
//   1. DC offset / subsonicos (<5 Hz): residuo de convolveres HRTF/RIR,
//      exciter no lineal (Volterra H2) y waveshapers — desplaza el punto
//      de operacion del limiter y genera intermodulacion audible.
//   2. NaN/Inf: cualquier inestabilidad numerica upstream (div/0 en un
//      filtro, overflow de un polinomio) se convierte en 0.0 en vez de
//      propagarse al DAC como ruido blanco a full scale.
//
// Filtro: y[n] = x[n] - x[n-1] + R*y[n-1], R = exp(-2*pi*5Hz/SR).
// Fase: minima en banda audible (polo a 5 Hz, -0.02 dB @ 20 Hz).
// Estado: 2 floats por canal, cero allocations, RT-safe.
#include <cmath>
#include <cstdint>
namespace ivanna {
struct DcBlocker {
    float x1 = 0.f, y1 = 0.f, R = 0.995f;
    void init(uint32_t sampleRate) noexcept {
        R = std::exp(-2.f * 3.14159265f * 5.f / (float)(sampleRate ? sampleRate : 48000));
        x1 = y1 = 0.f;
    }
    inline float process(float x) noexcept {
        if (!std::isfinite(x)) x = 0.f;             // sanitizer NaN/Inf
        const float y = x - x1 + R * y1;            // DC-block
        x1 = x; y1 = y;
        return std::isfinite(y) ? y : 0.f;
    }
};
} // namespace ivanna
