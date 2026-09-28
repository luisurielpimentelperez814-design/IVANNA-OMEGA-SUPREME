// MusicFeatureExtractor.hpp — extracción de características musicales/acústicas
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// REGLA DE TIEMPO REAL: este extractor corre acumulando estadísticas ligeras
// por bloque. NO hace FFT pesada en el callback: usa filtros de un polo para
// bandas + Goertzel de baja resolución fuera del hot path si se requiere.
// Sin malloc en process(): todos los buffers se preasignan en prepare().
#pragma once
#include <cstddef>
#include <cmath>

namespace ivanna { namespace ime {

struct MusicFeatures {
    float rms = 0.f;             // nivel medio (lineal)
    float peak = 0.f;            // pico absoluto
    float crestDb = 0.f;         // 20*log10(peak/rms) — proxy de rango dinámico
    float bassRatio = 0.f;       // energía de graves / total   (0..1)
    float midRatio = 0.f;        // energía de medios / total
    float trebleRatio = 0.f;     // energía de agudos / total
    float stereoWidth = 0.f;     // 1 - correlación L/R  (0=mono, ~1=muy ancho)
    float transientRate = 0.f;   // onsets por bloque normalizado (0..1)
    float density = 0.f;         // proporción de muestras activas (0..1)
};

class MusicFeatureExtractor {
public:
    bool prepare(float sampleRate, int maxBlock) noexcept;
    void reset() noexcept;
    // RT-safe: sin malloc, sin locks. l/r pueden ser nullptr (mono).
    void processBlock(const float* l, const float* r, int frames) noexcept;
    MusicFeatures features() const noexcept { return acc_; }
private:
    float sr_ = 48000.f; int maxBlock_ = 0;
    // estados de filtros de un polo (bandas) — L y R comparten (mid/side)
    float lpSub_ = 0.f, lpLow_ = 0.f, lpMid_ = 0.f;   // LP acumuladores
    float hpState_ = 0.f;                              // HP para agudos
    float aSub_ = 0.f, aLow_ = 0.f, aMid_ = 0.f, aHigh_ = 0.f; // coeficientes
    float prevEnergy_ = 0.f;                           // para onsets
    MusicFeatures acc_{};
    // acumuladores internos por bloque
    float sumSq_ = 0.f, eSub_ = 0.f, eLow_ = 0.f, eMid_ = 0.f, eHigh_ = 0.f;
    float sumLR_ = 0.f, sumLL_ = 0.f, sumRR_ = 0.f;
    int   activeSamples_ = 0, onsets_ = 0, n_ = 0;
    static inline float sanitize(float v) noexcept { return std::isfinite(v)?v:0.f; }
};

}} // namespace ivanna::ime
