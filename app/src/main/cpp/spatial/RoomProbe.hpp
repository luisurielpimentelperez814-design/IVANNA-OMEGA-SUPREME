// RoomProbe.hpp
// ============================================================================
// IVANNA — Corrección acústica de SALA / BOCINAS por medición con el micrófono del teléfono
// ============================================================================
// Por altavoces (AUX, DAC, Bluetooth) la sala y las bocinas son la mayor variable del sonido: un "boom" a 120 Hz,
// una caída de graves por debajo de 60 Hz, un valle en presencia. Ninguna etapa del producto medía eso. Esto sí:
//
//   MEDIR   Tonos senoidales escalonados en rejilla de 1/3 de octava (27 puntos, 25 Hz–12.7 kHz). Por cada tono se
//           obtiene la amplitud con detección síncrona (lock-in con ventana Hann): inmune al ruido de la sala porque
//           solo "escucha" en la frecuencia del tono. El ruido de fondo se mide igual (grabación sin tono) y da el
//           SNR por punto; los puntos con SNR < 10 dB se descartan.
//   DISEÑAR Referencia = media (dB) de 250 Hz–2 kHz. Para cada banda de octava del EQ de 10 bandas se promedia la
//           desviación medida y se invierte. Como los filtros peaking vecinos se solapan, se refina de forma
//           iterativa contra la respuesta REAL de la cascada (mínimos cuadrados por relajación).
//   ACOTAR  Solo 50 Hz–6 kHz (el micrófono de un teléfono no es fiable fuera de ahí): las demás bandas quedan en 0.
//           Recortes hasta −9 dB y realces hasta +3 dB (un valle de sala no se rellena con ganancia: gasta margen
//           y excita la sala), zona muerta de 1 dB.
//
// Lo que NO hace (a propósito, y se dice en la UI): el micrófono del teléfono no está calibrado, así que corrige el
// BALANCE TONAL amplio (boom, caída de graves, presencia), no modos de sala estrechos ni la fase; y mide el punto
// donde está el teléfono, no toda la sala.
//
// Sin asignaciones ni E/S. Puro cálculo: se prueba en el host con una sala sintética.
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>

namespace ivanna::room {

constexpr int   kBands        = 10;
constexpr float kBandHz[kBands] = {31.5f, 63.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f};
constexpr int   kProbePoints  = 27;
constexpr float kRefLoHz      = 250.f;
constexpr float kRefHiHz      = 2000.f;
constexpr float kMinCorrHz    = 50.f;
constexpr float kMaxCorrHz    = 6000.f;
constexpr float kMaxCutDb     = -9.f;
constexpr float kMaxBoostDb   = 3.f;
constexpr float kDeadbandDb   = 1.f;
constexpr float kMinSnrDb     = 10.f;
constexpr float kBandQ        = 1.41f;       // ~1 octava

/// Frecuencia (Hz) del punto de sonda i = 0..26 (rejilla de 1/3 de octava desde 25 Hz).
inline float probeHz(int i) noexcept { return 25.0f * std::exp2(static_cast<float>(i) / 3.0f); }

/// Amplitud (lineal, pico) de la componente a `f` Hz en x[0..n), por detección síncrona con ventana Hann.
inline float lockInAmplitude(const float* x, std::size_t n, float sampleRate, float f) noexcept {
    if (x == nullptr || n < 16 || !(sampleRate > 0.0f) || !(f > 0.0f) || f >= 0.5f * sampleRate) return 0.0f;
    const double w0 = 2.0 * 3.14159265358979323846 * static_cast<double>(f) / static_cast<double>(sampleRate);
    double si = 0.0, sq = 0.0, sw = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = static_cast<double>(x[i]);
        if (!std::isfinite(s)) continue;
        const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(n - 1));
        const double ph = w0 * static_cast<double>(i);
        si += win * s * std::cos(ph);
        sq += win * s * std::sin(ph);
        sw += win;
    }
    if (sw <= 0.0) return 0.0f;
    return static_cast<float>(2.0 * std::sqrt(si * si + sq * sq) / sw);
}

inline float toDb(float lin) noexcept { return 20.0f * std::log10(std::max(lin, 1.0e-9f)); }

/// Magnitud (dB) a `f` Hz de un filtro peaking RBJ (fc, gainDb, Q) — la misma fórmula que AutoEqFilter::setBand.
inline float peakingMagDb(float fc, float gainDb, float q, float f, float sampleRate) noexcept {
    if (std::fabs(gainDb) < 1.0e-6f) return 0.0f;
    using C = std::complex<double>;
    const double A  = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * static_cast<double>(fc) / static_cast<double>(sampleRate);
    const double al = std::sin(w0) / (2.0 * static_cast<double>(q));
    const double a0 = 1.0 + al / A;
    const double b0 = (1.0 + al * A) / a0, b1 = (-2.0 * std::cos(w0)) / a0, b2 = (1.0 - al * A) / a0;
    const double a1 = (-2.0 * std::cos(w0)) / a0, a2 = (1.0 - al / A) / a0;
    const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(f) / static_cast<double>(sampleRate);
    const C z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    const C h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    return static_cast<float>(20.0 * std::log10(std::max(std::abs(h), 1.0e-12)));
}

/// Respuesta (dB) a `f` de la cascada completa de las 10 bandas con ganancias `g`.
inline float cascadeMagDb(const float* g, float f, float sampleRate) noexcept {
    float s = 0.0f;
    for (int b = 0; b < kBands; ++b) s += peakingMagDb(kBandHz[b], g[b], kBandQ, f, sampleRate);
    return s;
}

/**
 * Diseña la corrección. levelDb[i] = nivel medido (dB, relativo; solo importan las diferencias) en probeHz(i);
 * snrDb[i] = relación tono/ruido de fondo. Devuelve false (y out = 0) si no hay referencia fiable (< 3 puntos
 * válidos en 250 Hz–2 kHz). out[b] en dB para kBandHz[b], redondeado a 0.1 dB.
 */
inline bool designCorrection(const float* levelDb, const float* snrDb, float sampleRate, float* out) noexcept {
    for (int b = 0; b < kBands; ++b) out[b] = 0.0f;
    if (levelDb == nullptr || snrDb == nullptr || out == nullptr) return false;
    auto valid = [&](int i) { return std::isfinite(levelDb[i]) && std::isfinite(snrDb[i]) && snrDb[i] >= kMinSnrDb; };

    double refSum = 0.0; int refN = 0;
    for (int i = 0; i < kProbePoints; ++i) {
        const float f = probeHz(i);
        if (valid(i) && f >= kRefLoHz && f <= kRefHiHz) { refSum += levelDb[i]; ++refN; }
    }
    if (refN < 3) return false;
    const float ref = static_cast<float>(refSum / refN);

    float target[kBands] = {};
    bool  free_[kBands]  = {};
    for (int b = 0; b < kBands; ++b) {
        const float fc = kBandHz[b];
        if (fc < kMinCorrHz || fc > kMaxCorrHz) continue;        // fuera de la zona fiable: queda en 0 y congelada
        const float lo = fc / 1.41421356f, hi = fc * 1.41421356f;
        double s = 0.0; int n = 0;
        for (int i = 0; i < kProbePoints; ++i) {
            const float f = probeHz(i);
            if (valid(i) && f >= lo && f <= hi) { s += levelDb[i] - ref; ++n; }
        }
        if (n == 0) continue;                                     // sin datos fiables en la banda: no se toca
        const float dev = static_cast<float>(s / n);
        if (std::fabs(dev) < kDeadbandDb) continue;
        target[b] = std::clamp(-dev, kMaxCutDb, kMaxBoostDb);
        free_[b]  = true;
    }
    float c[kBands];
    for (int b = 0; b < kBands; ++b) c[b] = target[b];
    for (int it = 0; it < 8; ++it) {                              // relajación contra la respuesta real de la cascada
        for (int b = 0; b < kBands; ++b) {
            if (!free_[b]) continue;
            const float resp = cascadeMagDb(c, kBandHz[b], sampleRate);
            c[b] = std::clamp(c[b] + 0.7f * (target[b] - resp), kMaxCutDb, kMaxBoostDb);
        }
    }
    for (int b = 0; b < kBands; ++b) out[b] = free_[b] ? std::round(c[b] * 10.0f) / 10.0f : 0.0f;
    return true;
}

}  // namespace ivanna::room
