#include "MusicIntelligenceEngine.hpp"
#include <cmath>

namespace ivanna { namespace ime {

// Base de conocimiento. Los centroides salen de metodología documentada de
// producción (rangos tonales/dinámicos/espaciales típicos por época y estilo),
// plasmados también en music_intelligence/production_profiles/*.json. No son
// presets por artista: son regiones del espacio de características MEDIBLES.
const ProductionProfile MusicIntelligenceEngine::kProfiles[kNumProfiles] = {
    // style                 bass  treb crest wdt  trns dens | wfs  hrtf tilt dyn  env
    { "progressive_rock_70s", {0.34f,0.18f,0.55f,0.62f,0.45f,0.70f}, 0.80f,0.70f,-1.0f,0.90f,0.55f },
    { "analog_warm_60s",      {0.40f,0.12f,0.45f,0.35f,0.35f,0.65f}, 0.45f,0.45f,-2.0f,0.80f,0.40f },
    { "stadium_rock_80s",     {0.36f,0.22f,0.40f,0.60f,0.55f,0.75f}, 0.75f,0.60f, 1.0f,0.70f,0.60f },
    { "modern_compressed",    {0.33f,0.20f,0.18f,0.45f,0.40f,0.85f}, 0.40f,0.40f, 0.5f,0.40f,0.30f },
    { "jazz_live_room",       {0.30f,0.16f,0.62f,0.55f,0.50f,0.55f}, 0.70f,0.75f,-0.5f,0.95f,0.70f },
    { "electronic_dense",     {0.45f,0.26f,0.22f,0.50f,0.60f,0.90f}, 0.55f,0.50f, 1.5f,0.50f,0.35f },
};

MusicIntelligenceEngine::MusicIntelligenceEngine() = default;

MusicDecision MusicIntelligenceEngine::decide(const MusicFeatures& f) const noexcept {
    // Normalizar al espacio del centroide (crestDb escalado a /24).
    const float v[6] = {
        f.bassRatio, f.trebleRatio, f.crestDb/24.0f,
        f.stereoWidth, f.transientRate, f.density
    };
    int best = 0; float bestD = 1e9f;
    for (int p=0;p<kNumProfiles;++p){
        float d=0.f;
        for (int k=0;k<6;++k){ float df=v[k]-kProfiles[p].centroid[k]; d+=df*df; }
        if (d<bestD){ bestD=d; best=p; }
    }
    // confidence = inversa de la distancia, acotada a (0,1].
    float conf = 1.0f/(1.0f+std::sqrt(bestD));
    const ProductionProfile& pr = kProfiles[best];
    MusicDecision out;
    out.profileIndex=best; out.style=pr.style; out.confidence=conf;
    out.wfsSpread=pr.wfsSpread; out.hrtfDepth=pr.hrtfDepth;
    out.eqTiltDb=pr.eqTiltDb; out.dynamicsAmount=pr.dynamicsAmount; out.envDepth=pr.envDepth;
    return out;
}

}} // namespace ivanna::ime
