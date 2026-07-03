#include "anti_dolby.h"
#include <algorithm>
#include <cmath>

static inline float clampf(float v, float a, float b) {
    return (v < a) ? a : (v > b) ? b : v;
}

AntiDolbyState::AntiDolbyState() {
    widenerMultiplier.store(1.0f, std::memory_order_relaxed);
    targetWidener = 1.0f;
    smoothedWidener = 1.0f;
}

void AntiDolbyState::reset() {
    std::lock_guard<std::mutex> lk(mtx);
    targetWidener = 1.0f;
    smoothedWidener = 1.0f;
    widenerMultiplier.store(1.0f, std::memory_order_relaxed);
}

void AntiDolbyState::updateFromClassification(float speech, float music, float bass) {
    if (!std::isfinite(speech)) speech = 0.0f;
    if (!std::isfinite(music))  music  = 0.0f;
    if (!std::isfinite(bass))   bass   = 0.0f;

    // Heurística:
    // - Si hay mucho speech, reducir anchura lateral para mejorar claridad.
    // - Si domina music, incrementar ligeramente la anchura.
    // - Bass reduce algo la anchura (evitar pérdidas de foco).
    float base = 1.0f;
    float delta = (music - speech) * 0.6f - bass * 0.25f;
    float tgt = base + delta;

    // límites razonables
    tgt = clampf(tgt, 0.5f, 1.6f);

    {
        std::lock_guard<std::mutex> lk(mtx);
        targetWidener = tgt;
    }
}

void AntiDolbyState::tick(float dt) {
    if (dt <= 0.0f) dt = 1.0f / 48000.0f; // fallback pequeño
    float alpha;
    {
        std::lock_guard<std::mutex> lk(mtx);
        // Coeficientes RC -> alpha = 1 - exp(-dt / tau)
        float tau = (targetWidener < smoothedWidener) ? attackTau : releaseTau;
        alpha = 1.0f - std::expf(-dt / tau);
        smoothedWidener = smoothedWidener + alpha * (targetWidener - smoothedWidener);
    }
    // Publicar el valor suavizado (no espaciado de memory_order estricto requerido)
    widenerMultiplier.store(smoothedWidener, std::memory_order_relaxed);
}
