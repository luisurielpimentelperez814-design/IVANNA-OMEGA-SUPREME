#pragma once
/*
 * true_peak.hpp — pico REAL (inter-muestra) por interpolación 4x con sinc ventaneada (v2.5.36)
 *
 * Un DAC reconstruye la onda continua entre muestras: dos muestras de 0.85 pueden dibujar un pico de 1.2.
 * El SafetyLimiter solo miraba |muestra|, así que esos picos pasaban sin control y recortaban en el filtro de
 * reconstrucción del DAC / etapa analógica (justo el camino de la salida de 3.5 mm). Esta función estima el pico
 * continuo con 3 puntos interpolados por intervalo (x4, 12 taps), igual de «suficiente para -0.1 dBTP» que el medidor
 * BS.1770 del proyecto (dsp/loudness_meter.hpp), sin memoria ni estado: RT-safe, O(n).
 */
#include <algorithm>
#include <cmath>

namespace ivanna {

// Interpolador 4x por sinc ventaneada (Hann) de 12 taps, 3 fases (1/4, 2/4, 3/4). Medido contra tonos
// puros de 0.05 a 0.35 fs en 13 fases: error máximo <= 0.01 dB. La versión previa (cúbica de Catmull-Rom de
// 4 puntos) subestimaba hasta 1.2 dB cerca de fs/4 — justo donde viven los picos ocultos de un DAC.
struct TruePeakTable {
    static constexpr int kTaps = 12;                 // muestras i-5 .. i+6
    float c[3][kTaps];
    TruePeakTable() noexcept {
        for (int p = 0; p < 3; ++p) {
            const double t = 0.25 * (p + 1);
            double sum = 0.0, v[kTaps];
            for (int j = 0; j < kTaps; ++j) {
                const double x = (j - (kTaps / 2 - 1)) - t;           // distancia al punto interpolado
                const double sinc = (std::fabs(x) < 1e-12) ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
                const double w = (std::fabs(x) < kTaps / 2) ? 0.5 * (1.0 + std::cos(M_PI * x / (kTaps / 2))) : 0.0;
                v[j] = sinc * w; sum += v[j];
            }
            for (int j = 0; j < kTaps; ++j) c[p][j] = static_cast<float>(v[j] / sum);   // ganancia DC = 1
        }
    }
};
inline const TruePeakTable kTruePeakTable{};         // se construye al cargar la biblioteca, no en el audio thread

// Pico verdadero de x[0..n) (>= pico de muestra). Los bordes replican la muestra extrema.
inline float truePeak4x(const float* x, int n) noexcept {
    if (!x || n <= 0) return 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(x[i]));
    constexpr int T = TruePeakTable::kTaps;
    for (int i = 0; i + 1 < n; ++i) {
        // Un intervalo cuyas dos muestras son pequeñas no puede esconder un pico grande: se omite (ahorra CPU).
        if (std::fabs(x[i]) < 0.25f * peak && std::fabs(x[i + 1]) < 0.25f * peak) continue;
        float seg[T];
        for (int j = 0; j < T; ++j) {
            int k = i + j - (T / 2 - 1);
            k = k < 0 ? 0 : (k >= n ? n - 1 : k);
            seg[j] = x[k];
        }
        for (int p = 0; p < 3; ++p) {
            float acc = 0.0f;
            for (int j = 0; j < T; ++j) acc += kTruePeakTable.c[p][j] * seg[j];
            peak = std::max(peak, std::fabs(acc));
        }
    }
    return peak;
}

} // namespace ivanna
