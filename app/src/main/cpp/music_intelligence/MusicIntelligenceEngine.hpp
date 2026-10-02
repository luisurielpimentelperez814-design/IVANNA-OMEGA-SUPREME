// MusicIntelligenceEngine.hpp — Motor de Inteligencia Musical y Escena (Atlas 12D)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Integra el clasificador Bayesiano en variedad Gaussiana diagonal (StyleBlender, 12 estilos)
// con histéresis temporal, compuerta smoothstep de confianza y mezcla continua de objetivos.
#pragma once
#include "MusicFeatureExtractor.hpp"
#include "StyleBlender.hpp"

namespace ivanna { namespace ime {

class MusicIntelligenceEngine {
public:
    MusicIntelligenceEngine() noexcept { prepare(); }
    void prepare() noexcept;
    void reset() noexcept;
    void softReset() noexcept;

    // Decisión instantánea (sin memoria de histéresis previa, ideal para consulta aislada / test)
    StyleDecision decide(const MusicFeatures& f) const noexcept;

    // Decisión Bayesiana con estado (suavizado exponencial tau=2s, histéresis de 3 pasos, slew limiter)
    StyleDecision updateStateful(const MusicFeatures& f, float dtSec, int manualOverrideIdx = -1) noexcept;

    int numProfiles() const noexcept;
    const StyleProfile& profile(int idx) const noexcept;
    const StyleBlender& blender() const noexcept { return blender_; }
    StyleBlender& blender() noexcept { return blender_; }

private:
    StyleBlender blender_{};
    StyleProfile legacyProfiles_[kDefaultAtlasStyles]{};
};

}} // namespace ivanna::ime
