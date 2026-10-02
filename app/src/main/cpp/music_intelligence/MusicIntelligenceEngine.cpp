#include "MusicIntelligenceEngine.hpp"
#include <cmath>
#include <algorithm>

namespace ivanna { namespace ime {

void MusicIntelligenceEngine::prepare() noexcept {
    int n = 0;
    const StyleProto* lib = defaultAtlasLibrary(n);
    blender_.setLibrary(lib, n);
    for (int i = 0; i < n && i < kDefaultAtlasStyles; ++i) {
        StyleProfile& p = legacyProfiles_[i];
        p.name           = lib[i].name;
        p.bassRatio      = lib[i].mu[0];
        p.trebleRatio    = lib[i].mu[1];
        p.crest          = lib[i].mu[2];
        p.width          = lib[i].mu[3];
        p.transients     = lib[i].mu[4];
        p.density        = lib[i].mu[5];
        p.wfsSpread      = lib[i].t.wfsSpread;
        p.hrtfDepth      = lib[i].t.hrtfDepth;
        p.eqTiltDb       = lib[i].t.eqTiltDb;
        p.dynamicsAmount = lib[i].t.dynamicsAmount;
        p.envDepth       = lib[i].t.envDepth;
        p.warmth         = lib[i].t.warmth;
    }
}

void MusicIntelligenceEngine::reset() noexcept {
    blender_.reset();
}

void MusicIntelligenceEngine::softReset() noexcept {
    blender_.softReset();
}

int MusicIntelligenceEngine::numProfiles() const noexcept {
    return kDefaultAtlasStyles;
}

const StyleProfile& MusicIntelligenceEngine::profile(int idx) const noexcept {
    if (idx < 0 || idx >= kDefaultAtlasStyles) return legacyProfiles_[0];
    return legacyProfiles_[idx];
}

StyleDecision MusicIntelligenceEngine::decide(const MusicFeatures& f) const noexcept {
    StyleDecision out;
    if (f.rms < 1e-4f) {
        return out; // silencio -> neutral exacto
    }
    float vec12[kDim]{};
    f.toVector12(vec12);

    // Evaluador puntual determinista (sin estado previo):
    // En consulta puntual (decide), usamos dtSec = 20.0 s para converger en 1 paso
    // a la distribución posterior estacionaria del vector f.
    StyleBlender localBlender;
    out = localBlender.update(vec12, 20.0f, -1);
    return out;
}

StyleDecision MusicIntelligenceEngine::updateStateful(const MusicFeatures& f,
                                                      float dtSec,
                                                      int manualOverrideIdx) noexcept {
    if (f.rms < 1e-4f && manualOverrideIdx < 0) {
        StyleDecision silentOut;
        return silentOut;
    }
    float vec12[kDim]{};
    f.toVector12(vec12);
    return blender_.update(vec12, dtSec, manualOverrideIdx);
}

}} // namespace ivanna::ime
