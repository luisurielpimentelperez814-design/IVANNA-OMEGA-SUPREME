// ============================================================================
// routea_core.h — Núcleo resampleador PURO derivado del RouteAAsrc de Ruta A.
// v2.5.397 (rectificación magistral del daemon ASRC)
//
// SOLO el remuestreo: FIR 319 taps (tabla fir_v5, 2048 fases) + interpolación
// cúbica Catmull-Rom entre fases con las filas de borde DESPLAZADAS un tap
// (el fix que da THD+N -95 dB; el v6 clampaba y dejaba un glitch de 1.1e-4).
// SIN Ψ (el campo vive en la app, no en el daemon), SIN stage timings,
// SIN símbolos débiles, SIN atómicos (el daemon lo llama desde un solo hilo).
//
// El resampleador produce un número VARIABLE de frames (N*ratio); el bloque
// fijo (salida == entrada, lo que exige AudioFlinger) lo garantiza la FIFO
// de salida de ivanna_asrc_core, NO este núcleo.
// ============================================================================
#pragma once

#include <cstdint>

namespace ivanna {
namespace routea {

// Dimensiones de la tabla FIR (única fuente: fir_v5_coeffs.h, generada por
// gen_fir_v5.py). 2048 fases y 319 taps. Retardo de grupo ~32 muestras.
inline constexpr int kRouteaTaps = 319;
inline constexpr int kRouteaPhases = 2048;
inline constexpr int kRouteaHistFrames = 2 * kRouteaTaps;

// Ratio permitido: ±500 ppm (riel del daemon/servo; el PLL de Ruta A ya
// limita a ±500 ppm). Fuera del riel o no finito -> 1.0 (fail-closed).
inline constexpr double kRouteaRatioMin = 1.0 - 500e-6;
inline constexpr double kRouteaRatioMax = 1.0 + 500e-6;

class Core {
public:
    Core();
    ~Core() = default;
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = default;
    Core& operator=(Core&&) = default;

    // ratio = salida/entrada. Clampeado a ±500 ppm; NaN/no-finito -> 1.0.
    // Devuelve el ratio aplicado.
    double setRatio(double ratio);
    double ratio() const { return ratio_; }

    // Consume `inFrames` (estéreo intercalado float L/R) y produce la
    // salida remuestreada en `out` (capacidad `outCap` frames).
    // Devuelve los frames producidos (0 si no hay nada que producir).
    // `outCap` debe ser >= inFrames * 1.002 + 64.
    // El estado (historia FIR, posición fraccionaria) persiste entre
    // bloques: la salida es continua, sin discontinuidad de fase.
    int process(const float* in, int inFrames, float* out, int outCap);

    void reset();

    uint64_t totalInFrames() const { return totalIn_; }
    uint64_t totalOutFrames() const { return totalOut_; }

private:
    void firCoeffsForFrac(double frac);
    void histPush(float l, float r);

    double ratio_ = 1.0;

    // Posición de entrada (frames, relativa al bloque actual) de la
    // próxima muestra de salida. Carry entre bloques, en [-1, 0].
    double pos_ = 0.0;

    // Historia FIR: buffers circulares espejados (2*319 frames por canal).
    float histL_[kRouteaHistFrames];
    float histR_[kRouteaHistFrames];
    int histW_ = 0;
    int32_t histFilled_ = 0;
    float coeff_[kRouteaTaps];

    uint64_t totalIn_ = 0;
    uint64_t totalOut_ = 0;
};

}  // namespace routea
}  // namespace ivanna
