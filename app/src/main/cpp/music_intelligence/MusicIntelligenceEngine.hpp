// MusicIntelligenceEngine.hpp — "ingeniero de sonido virtual" embebido.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS.
//
// Clasifica la producción por características MEDIBLES (no por nombre de
// artista ni presets) contra una base de perfiles con centroides derivados de
// metodología documentada de producción musical (ver docs/MUSIC_INTELLIGENCE.md
// y music_intelligence/production_profiles/*.json). La decisión produce
// objetivos DSP REALES (wfsSpread, hrtfDepth, eqTiltDb, dynamicsAmount,
// envDepth) que el a aplicador aplica a los motores existentes.
#pragma once
#include "MusicFeatureExtractor.hpp"
#include <cstddef>

namespace ivanna { namespace ime {

struct ProductionProfile {
    const char* style;
    // centroide de características (orden: bassRatio, trebleRatio, crestDb/24,
    // stereoWidth, transientRate, density)
    float centroid[6];
    // objetivos DSP recomendados
    float wfsSpread;      // 0..1 apertura de campo
    float hrtfDepth;      // 0..1 profundidad binaural
    float eqTiltDb;       // -6..+6 dB inclinación tonal
    float dynamicsAmount; // 0..1 preservación dinámica (1=no comprimir)
    float envDepth;       // 0..1 ambiente/reverb
};

struct MusicDecision {
    int   profileIndex = -1;
    const char* style = "unknown";
    float confidence = 0.f;
    float wfsSpread = 0.5f, hrtfDepth = 0.5f, eqTiltDb = 0.f, dynamicsAmount = 1.f, envDepth = 0.3f;
};

class MusicIntelligenceEngine {
public:
    MusicIntelligenceEngine();
    // Clasifica las características y devuelve la decisión DSP. Determinista,
    // sin asignación de memoria. confidence en (0,1].
    MusicDecision decide(const MusicFeatures& f) const noexcept;
    const ProductionProfile* profiles() const noexcept { return kProfiles; }
    static constexpr int kNumProfiles = 6;
private:
    static const ProductionProfile kProfiles[kNumProfiles];
};

}} // namespace ivanna::ime
