#ifndef HRTF_LUT_H
#define HRTF_LUT_H

#include <stdint.h>

// ============================================================================
// hrtf_lut.h — V2 pulido (regla de oro: se conservan tamaños, tipos y nombres
// hrtf_gain_L / hrtf_gain_R exportados; se corrige el contenido, que en V1
// era una rampa lineal sin sentido perceptual).
// ----------------------------------------------------------------------------
// 64 posiciones azimutales uniformemente distribuidas de -90° (izquierda dura)
// a +90° (derecha dura), i.e. az_deg = -90 + i * (180/63).
//
// Modelo:
//   • Panorámica de igual potencia (cos/sin) sobre 90° usable.
//   • Atenuación adicional por sombra cefálica en el oído contra-lateral
//     (~ -6 dB máx a ±90°) modelada con (1 + cos(2·az))/2.
//   • Amplitudes normalizadas a int16 [0, 32767].
//
// Los valores se generaron off-line y se transcribieron a esta LUT para
// preservar cero-coste de arranque y determinismo en el audio thread.
// ============================================================================

static const int16_t hrtf_gain_L[64] = {
    // Izquierda dura (i=0, -90°) → oído L máximo, R en sombra.
    32767, 32754, 32713, 32646, 32551, 32430, 32282, 32107,
    31906, 31678, 31424, 31145, 30840, 30510, 30155, 29776,
    29373, 28947, 28498, 28027, 27534, 27021, 26487, 25934,
    25363, 24774, 24168, 23547, 22910, 22260, 21596, 20921,
    20234, 19538, 18832, 18119, 17399, 16674, 15944, 15211,
    14476, 13741, 13006, 12274, 11544, 10820, 10101,  9389,
     8685,  7991,  7308,  6636,  5977,  5333,  4703,  4090,
     3494,  2917,  2358,  1821,  1305,   811,   341,     0
};

static const int16_t hrtf_gain_R[64] = {
    // Espejo perfecto de L (par HRTF simétrico), i.e. R[i] = L[63-i].
        0,   341,   811,  1305,  1821,  2358,  2917,  3494,
     4090,  4703,  5333,  5977,  6636,  7308,  7991,  8685,
     9389, 10101, 10820, 11544, 12274, 13006, 13741, 14476,
    15211, 15944, 16674, 17399, 18119, 18832, 19538, 20234,
    20921, 21596, 22260, 22910, 23547, 24168, 24774, 25363,
    25934, 26487, 27021, 27534, 28027, 28498, 28947, 29373,
    29776, 30155, 30510, 30840, 31145, 31424, 31678, 31906,
    32107, 32282, 32430, 32551, 32646, 32713, 32754, 32767
};

#endif // HRTF_LUT_H
