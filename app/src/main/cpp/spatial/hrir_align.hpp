// ============================================================================
// hrir_align.hpp — alineación temporal de HRIR para interpolar sin emborronar
// ============================================================================
// Promediar dos HRIR vecinas muestra a muestra mezcla impulsos que llegan en instantes
// distintos (la ITD cambia hasta ~4-5 muestras entre nodos de 15° a 48 kHz): el resultado son
// DOS impulsos de media amplitud, es decir un peine en frecuencia (muesca hacia fs/(2·Δ)), que
// apaga agudos y desdibuja la diferencia interaural justo donde el oído localiza. La
// interpolación correcta alinea cada HRIR por su comienzo (onset), promedia las formas ya
// alineadas y vuelve a colocar el resultado en el onset promedio (ponderado).
//
// Solo cabecera, sin asignaciones ni excepciones: apta para hilo de audio y para -fno-exceptions.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Ivanna { namespace hrir {

// Un onset se declara cuando la HRIR supera esta fracción de su pico.
inline constexpr float kOnsetFraction = 0.2f;
// Un desplazamiento mayor (muestras) indica un onset poco fiable: no se alinea (se promedia sin mover).
inline constexpr double kMaxAlignShift = 40.0;

// Posición fraccional (en muestras) donde la HRIR cruza kOnsetFraction·pico. 0 si no hay señal.
inline double onsetSamples(const float* h, size_t len) noexcept {
    if (!h || len == 0) return 0.0;
    float peak = 0.0f;
    for (size_t i = 0; i < len; ++i) peak = std::max(peak, std::fabs(h[i]));
    if (!(peak > 1.0e-12f)) return 0.0;
    const float thr = kOnsetFraction * peak;
    for (size_t i = 0; i < len; ++i) {
        const float a = std::fabs(h[i]);
        if (a >= thr) {
            if (i == 0) return 0.0;
            const float p = std::fabs(h[i - 1]);
            const float d = a - p;
            const double frac = d > 1.0e-20f ? static_cast<double>((thr - p) / d) : 1.0;
            return static_cast<double>(i - 1) + std::clamp(frac, 0.0, 1.0);
        }
    }
    return 0.0;
}

// Muestra de h en la posición fraccional pos (Catmull-Rom de 4 puntos: pierde mucho menos
// agudo que la lineal). Fuera de [0, len-1] vale 0: una HRIR no tiene energía antes ni después.
inline double sampleAt(const float* h, size_t len, double pos) noexcept {
    if (!h || len == 0 || !(pos > -1.0) || !(pos < static_cast<double>(len))) return 0.0;
    const double fl = std::floor(pos);
    const double t = pos - fl;
    const long i = static_cast<long>(fl);
    auto at = [&](long k) -> double {
        return (k < 0 || k >= static_cast<long>(len)) ? 0.0 : static_cast<double>(h[k]);
    };
    const double p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 +
                                           t * (3.0 * (p1 - p2) + p3 - p0)));
}

// Desplazamiento (muestras) con el que hay que leer una HRIR cuyo onset es `onset` para que
// quede en `target`; 0 si es demasiado grande (onset dudoso).
inline double alignShift(double onset, double target) noexcept {
    const double s = onset - target;
    return std::fabs(s) <= kMaxAlignShift ? s : 0.0;
}

}} // namespace Ivanna::hrir
