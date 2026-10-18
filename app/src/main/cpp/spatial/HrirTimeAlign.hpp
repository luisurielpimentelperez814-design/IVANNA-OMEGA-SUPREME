// ============================================================================
// HrirTimeAlign.hpp — interpolación de HRIR ALINEADA EN EL TIEMPO
//
// PROBLEMA: promediar muestra a muestra dos HRIR vecinas mezcla dos impulsos con
// distinta llegada (ITD): el resultado es un PEINE de dos impulsos —muescas de
// -inf dB en fs/(2·Δτ)— que se come agudos y degrada la localización justo donde el
// oído más la usa. Cuanto mayor la separación angular entre vecinas, peor.
//
// SOLUCIÓN (estándar en HRTF de calidad): separar «cuándo llega» de «cómo suena».
//   1. Se mide el ONSET sub-muestra de cada HRIR (cruce de un umbral relativo al pico,
//      interpolado linealmente entre dos muestras).
//   2. El onset objetivo es la media ponderada de los onsets vecinos: la ITD resultante
//      es la interpolación lineal de las ITD, que es lo físicamente razonable.
//   3. Cada vecina se desplaza con RETARDO FRACCIONAL (sinc con ventana Blackman de 16
//      puntos, ganancia DC = 1) hasta ese onset y SOLO ENTONCES se suman con sus pesos.
//
// GUARDAS (no inventan nada; ante la duda se degrada al método clásico):
//   · un onset no medible (HRIR muda/NaN) o una separación de onsets mayor que
//     kMaxAlignSamples => se usa la suma sin alinear (el comportamiento anterior);
//   · desplazamientos < kMinShift => se suma tal cual (ahorra el kernel).
//
// RT-safe: sin malloc, sin locks, sin excepciones; coste acotado y fijo.
// Cabecera sola: la usan el constructor de la tabla (offline) y el interpolador (RT).
// ============================================================================
#pragma once

#include <cmath>
#include <cstddef>

namespace Ivanna {
namespace hrirAlign {

constexpr int    kHalfWidth       = 8;       // kernel de 2*kHalfWidth = 16 puntos
constexpr float  kOnsetThreshold  = 0.20f;   // fracción del pico absoluto
constexpr float  kMaxAlignSamples = 24.0f;   // ~0.5 ms @48k: cubre vecinas de hasta ~30° cerca del frente (≈0.65 muestras/°); más es un onset corrupto
constexpr float  kMinShift        = 0.01f;

/** Onset sub-muestra (en muestras, >= 0) o -1 si la HRIR no es medible. */
inline float onsetSamples(const float* h, size_t n) noexcept {
    if (!h || n == 0) return -1.0f;
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float a = std::fabs(h[i]);
        if (!std::isfinite(a)) return -1.0f;
        if (a > peak) peak = a;
    }
    if (!(peak > 1.0e-9f)) return -1.0f;
    const float thr = kOnsetThreshold * peak;
    for (size_t i = 0; i < n; ++i) {
        const float a = std::fabs(h[i]);
        if (a >= thr) {
            if (i == 0) return 0.0f;
            // Cruce sub-muestra sobre la envoltura rectificada con Hermite cúbico (tangentes centrales) y
            // bisección: la interpolación lineal sesga el onset hasta ±0.17 muestras según la fase de muestreo.
            const float e0 = std::fabs(h[i - 1]), e1 = a;
            const float em = (i >= 2) ? std::fabs(h[i - 2]) : e0;
            const float ep = (i + 1 < n) ? std::fabs(h[i + 1]) : e1;
            const float m0 = 0.5f * (e1 - em), m1 = 0.5f * (ep - e0);
            float lo = 0.0f, hi = 1.0f;
            for (int it = 0; it < 24; ++it) {
                const float t = 0.5f * (lo + hi);
                const float t2 = t * t, t3 = t2 * t;
                const float v = (2*t3 - 3*t2 + 1) * e0 + (t3 - 2*t2 + t) * m0 + (-2*t3 + 3*t2) * e1 + (t3 - t2) * m1;
                if (v < thr) lo = t; else hi = t;
            }
            return static_cast<float>(i - 1) + 0.5f * (lo + hi);
        }
    }
    return -1.0f;
}

/** Pesos del interpolador sinc-Blackman para una fracción f en [0,1): k[j+7], j=-7..8; suman 1. */
inline void fracKernel(float f, float* k) noexcept {
    constexpr double pi = 3.14159265358979323846;
    double sum = 0.0;
    for (int j = -(kHalfWidth - 1); j <= kHalfWidth; ++j) {
        const double x = static_cast<double>(f) - static_cast<double>(j);
        const double s = (std::fabs(x) < 1.0e-9) ? 1.0 : std::sin(pi * x) / (pi * x);
        const double u = x / static_cast<double>(kHalfWidth);
        const double w = (std::fabs(u) >= 1.0) ? 0.0
                       : 0.42 + 0.5 * std::cos(pi * u) + 0.08 * std::cos(2.0 * pi * u);
        const double v = s * w;
        k[j + (kHalfWidth - 1)] = static_cast<float>(v);
        sum += v;
    }
    const float inv = static_cast<float>(1.0 / sum);
    for (int i = 0; i < 2 * kHalfWidth; ++i) k[i] *= inv;
}

/** dst[t] += weight * src(t - delay), con src fuera de rango = 0. delay > 0 retrasa. */
inline void accumulateShifted(const float* src, size_t srcLen, float delay, float weight,
                              float* dst, size_t dstLen) noexcept {
    if (!src || !dst) return;
    if (std::fabs(delay) < kMinShift) {
        const size_t n = srcLen < dstLen ? srcLen : dstLen;
        for (size_t t = 0; t < n; ++t) dst[t] += weight * src[t];
        return;
    }
    const float s  = -delay;                       // posición de lectura = t + s
    const float fl = std::floor(s);
    const float f  = s - fl;
    const long  base = static_cast<long>(fl);
    float k[2 * kHalfWidth];
    fracKernel(f, k);
    const long len = static_cast<long>(srcLen);
    for (size_t t = 0; t < dstLen; ++t) {
        float acc = 0.0f;
        const long i0 = static_cast<long>(t) + base;
        for (int j = -(kHalfWidth - 1); j <= kHalfWidth; ++j) {
            const long idx = i0 + j;
            if (idx >= 0 && idx < len) acc += k[j + (kHalfWidth - 1)] * src[idx];
        }
        dst[t] += weight * acc;
    }
}

/**
 * Mezcla ponderada ALINEADA de `count` HRIR de un mismo oído hacia `dst` (que el llamador deja a cero).
 * `onsets[k]` es el onset ya medido de la vecina k (o -1). Devuelve true si alineó; false si degradó a la
 * suma clásica (onset no medible o separación > kMaxAlignSamples). Los pesos NO necesitan sumar 1: se normalizan.
 */
inline bool blendAligned(const float* const* src, const size_t* srcLen, const float* onsets,
                         const float* weights, size_t count, float* dst, size_t dstLen) noexcept {
    double wSum = 0.0;
    for (size_t k = 0; k < count; ++k) if (weights[k] > 0.0f) wSum += weights[k];
    if (!(wSum > 0.0)) return false;
    const float inv = static_cast<float>(1.0 / wSum);

    bool ok = true;
    double tau = 0.0;
    float tmin = 1.0e30f, tmax = -1.0e30f;
    for (size_t k = 0; k < count; ++k) {
        if (!(weights[k] > 0.0f)) continue;
        if (!(onsets[k] >= 0.0f) || !std::isfinite(onsets[k])) { ok = false; break; }
        tau += static_cast<double>(weights[k]) * onsets[k];
        if (onsets[k] < tmin) tmin = onsets[k];
        if (onsets[k] > tmax) tmax = onsets[k];
    }
    if (ok && (tmax - tmin) > kMaxAlignSamples) ok = false;
    const float target = ok ? static_cast<float>(tau * inv) : 0.0f;

    for (size_t k = 0; k < count; ++k) {
        if (!(weights[k] > 0.0f)) continue;
        const float delay = ok ? (target - onsets[k]) : 0.0f;
        accumulateShifted(src[k], srcLen[k], delay, weights[k] * inv, dst, dstLen);
    }
    return ok;
}

} // namespace hrirAlign
} // namespace Ivanna
