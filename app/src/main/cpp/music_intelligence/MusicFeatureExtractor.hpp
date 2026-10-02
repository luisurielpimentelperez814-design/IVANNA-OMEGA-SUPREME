// MusicFeatureExtractor.hpp — extracción de características musicales/acústicas 12D (Atlas v2)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// REGLA DE TIEMPO REAL: este extractor corre acumulando estadísticas ligeras
// por bloque con filtros complementarios de un polo y bucle SIMD ARM64 NEON.
// Cero malloc/locks/excepciones en processBlock().
//
// Vector Atlas f[12] (Sección 2):
//   f0  bassRatio        E(20-250)/E_total       [capa fundamental]
//   f1  trebleRatio      E(>2k)/E_total          [presencia/aire legacy]
//   f2  crest24          crestDb/24              [dinámica D]
//   f3  stereoWidth      1 - rho_LR              [movimiento M]
//   f4  transientRate    onsets norm. [0,1]      [capa transitoria]
//   f5  density          muestras activas [0,1]  [textura H/T]
//   f6  presenceRatio    E(3-8k)/E_total         [banda Presencia Atlas]
//   f7  airRatio         E(8-20k)/E_total        [banda Aire Atlas]
//   f8  flatness1m       1 - SFM [0,1]           [densidad armónica H]
//   f9  onsetRegularity  1 - CV(IOI) [0,1]       [ritmo R: groove vs polímetro]
//   f10 lraProxy12       std(loudness_st)/12 dB  [crecimiento orgánico D]
//   f11 sideMid          E_S / (E_M + E_S)       [ambiente M/S]
#pragma once
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace ivanna { namespace ime {

struct MusicFeatures {
    float rms = 0.f;             // nivel medio (lineal)
    float peak = 0.f;            // pico absoluto
    float crestDb = 0.f;         // 20*log10(peak/rms) — proxy de rango dinámico
    float bassRatio = 0.f;       // f0: energía de graves / total   (0..1)
    float midRatio = 0.f;        // energía de medios / total
    float trebleRatio = 0.f;     // f1: energía de agudos / total
    float stereoWidth = 0.f;     // f3: 1 - correlación L/R  (0=mono, ~1=muy ancho)
    float transientRate = 0.f;   // f4: onsets por bloque normalizado (0..1)
    float density = 0.f;         // f5: proporción de muestras activas (0..1)

    // Nuevas dimensiones medibles del Atlas Acústico (f6..f11)
    float presenceRatio = 0.f;   // f6: E(3-8 kHz) / E_total (0..1)
    float airRatio = 0.f;        // f7: E(8-20 kHz) / E_total (0..1)
    float flatness1m = 0.f;      // f8: 1 - Spectral Flatness Measure (0..1)
    float onsetRegularity = 0.5f;// f9: 1 - CV(IOI) (0..1)
    float lraProxy12 = 0.f;      // f10: desv. est. loudness corto plazo / 12 dB (0..1)
    float sideMid = 0.f;         // f11: E_S / (E_M + E_S) (0..1)

    // Bandas complementarias del Atlas (energía relativa normalizada para UI/telemetría)
    float subBandRatio = 0.f;    // Sub 20-80 Hz
    float bodyBandRatio = 0.f;   // Cuerpo 80-400 Hz
    float defBandRatio = 0.f;    // Definición 400-3k Hz

    // Empaqueta el vector canónico f[12] para StyleBlender / exportación
    void toVector12(float out[12]) const noexcept {
        if (!out) return;
        out[0]  = bassRatio;
        out[1]  = trebleRatio;
        out[2]  = std::clamp(crestDb / 24.0f, 0.0f, 1.0f);
        out[3]  = std::clamp(stereoWidth, 0.0f, 1.0f);
        out[4]  = std::clamp(transientRate, 0.0f, 1.0f);
        out[5]  = std::clamp(density, 0.0f, 1.0f);
        out[6]  = std::clamp(presenceRatio, 0.0f, 1.0f);
        out[7]  = std::clamp(airRatio, 0.0f, 1.0f);
        out[8]  = std::clamp(flatness1m, 0.0f, 1.0f);
        out[9]  = std::clamp(onsetRegularity, 0.0f, 1.0f);
        out[10] = std::clamp(lraProxy12, 0.0f, 1.0f);
        out[11] = std::clamp(sideMid, 0.0f, 1.0f);
    }
};

class MusicFeatureExtractor {
public:
    bool prepare(float sampleRate, int maxBlock) noexcept;
    void reset() noexcept;
    // RT-safe: sin malloc, sin locks. l/r pueden ser nullptr (mono).
    void processBlock(const float* l, const float* r, int frames) noexcept;
    MusicFeatures features() const noexcept { return acc_; }
    float sampleRate() const noexcept { return sr_; }

private:
    static constexpr int kIoiHistSize = 16;
    static constexpr int kLraHistSize = 32;

    float sr_ = 48000.f;
    int   maxBlock_ = 0;

    // Estados de filtros de un polo legacy (f0..f1 intactos)
    float lpSub_ = 0.f, lpLow_ = 0.f, lpMid_ = 0.f;
    float hpState_ = 0.f;
    float aSub_ = 0.f, aLow_ = 0.f, aMid_ = 0.f, aHigh_ = 0.f;

    // Partición complementaria exacta de 5 bandas del Atlas (suma(bandas) == mid)
    // Sub (0-80), Cuerpo (80-400), Definición (400-3k), Presencia (3k-8k), Aire (>8k)
    float lp80_ = 0.f, lp400_ = 0.f, lp3k_ = 0.f, lp8k_ = 0.f;
    float a80_ = 0.f, a400_ = 0.f, a3k_ = 0.f, a8k_ = 0.f;

    // Detector de onsets y regularidad rítmica IOI (f9)
    float prevEnergy_ = 0.f;
    int   samplesSinceOnset_ = 0;
    int   minOnsetGapSamples_ = 960; // 20 ms refractario @fs
    float ioiSecHist_[kIoiHistSize]{};
    int   ioiCount_ = 0;
    int   ioiWriteIdx_ = 0;

    // Historial de loudness de corto plazo (en dB) para LRA proxy (f10)
    float lraDbHist_[kLraHistSize]{};
    int   lraCount_ = 0;
    int   lraWriteIdx_ = 0;

    MusicFeatures acc_{};

    // Acumuladores internos por bloque
    float sumSq_ = 0.f, eSub_ = 0.f, eLow_ = 0.f, eMid_ = 0.f, eHigh_ = 0.f;
    float sumLR_ = 0.f, sumLL_ = 0.f, sumRR_ = 0.f;
    int   activeSamples_ = 0, onsets_ = 0, n_ = 0;

    static inline float sanitize(float v) noexcept { return std::isfinite(v) ? v : 0.f; }
};

}} // namespace ivanna::ime
