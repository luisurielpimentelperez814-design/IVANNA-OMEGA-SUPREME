#include "anti_dolby.h"
#include <algorithm>
#include <cmath>

// ============================================================================
// AntiDolbyState — V2 pulido (no se borra nada; se afinan constantes,
// robustez numérica y se añaden setters seguros).
// ============================================================================

namespace {
inline float clampf(float v, float a, float b) {
    return (v < a) ? a : (v > b) ? b : v;
}
} // namespace

AntiDolbyState::AntiDolbyState() {
    widenerMultiplier.store(1.0f, std::memory_order_relaxed);
    targetWidener   = 1.0f;
    smoothedWidener = 1.0f;
}

void AntiDolbyState::reset() {
    std::lock_guard<std::mutex> lk(mtx);
    targetWidener   = 1.0f;
    smoothedWidener = 1.0f;
    widenerMultiplier.store(1.0f, std::memory_order_release);
}

void AntiDolbyState::updateFromClassification(float speech, float music, float bass) {
    if (!std::isfinite(speech)) speech = 0.0f;
    if (!std::isfinite(music))  music  = 0.0f;
    if (!std::isfinite(bass))   bass   = 0.0f;

    // Los pesos exactos vienen del análisis perceptual V1 (mantenidos):
    //   • speech ↑ ⇒ estrechar (inteligibilidad).
    //   • music  ↑ ⇒ ensanchar (envoltura).
    //   • bass   ↑ ⇒ estrechar levemente (foco monofónico de graves).
    const float base  = 1.0f;
    const float delta = (music - speech) * 0.6f - bass * 0.25f;
    const float tgt   = clampf(base + delta, 0.5f, 1.6f);

    std::lock_guard<std::mutex> lk(mtx);
    targetWidener = tgt;
}

void AntiDolbyState::tick(float dt) {
    // Robustez: dt fuera de rango ⇒ paso conservador (1 muestra a 48 kHz).
    if (!std::isfinite(dt) || dt <= 0.0f) dt = 1.0f / 48000.0f;
    if (dt > 0.1f)                        dt = 0.1f;   // no permitir >100 ms

    float published;
    {
        std::lock_guard<std::mutex> lk(mtx);
        const float tau   = (targetWidener < smoothedWidener) ? attackTau : releaseTau;
        const float alpha = 1.0f - std::exp(-dt / tau);           // float overload
        smoothedWidener  += alpha * (targetWidener - smoothedWidener);
        published         = smoothedWidener;
    }
    // Publicación con release ⇒ el consumidor puede usar acquire y ver el
    // último snapshot coherente.
    widenerMultiplier.store(published, std::memory_order_release);
}

void AntiDolbyState::setAttackTau(float seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return;
    std::lock_guard<std::mutex> lk(mtx);
    attackTau = seconds;
}

void AntiDolbyState::setReleaseTau(float seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return;
    std::lock_guard<std::mutex> lk(mtx);
    releaseTau = seconds;
}
