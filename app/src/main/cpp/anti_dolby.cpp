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
    // Reset de parámetros de suavizado
    {
        std::lock_guard<std::mutex> lk(mtx);
        targetWidener   = 1.0f;
        smoothedWidener = 1.0f;
    }
    
    // Reset de clasificación y parámetros DSP (sin lock, son atomics)
    speechScore.store(0.0f, std::memory_order_relaxed);
    musicScore.store(0.0f, std::memory_order_relaxed);
    bassScore.store(0.0f, std::memory_order_relaxed);
    classificationValid.store(false, std::memory_order_relaxed);
    
    widenerMultiplier.store(1.0f, std::memory_order_release);
    eqBoost2k4k.store(0.0f, std::memory_order_relaxed);
    exciterLowOnly.store(false, std::memory_order_relaxed);
    
    frameCounter.store(0, std::memory_order_relaxed);
}

void AntiDolbyState::updateFromClassification(float speech, float music, float bass) {
    // Sanidad numérica
    if (!std::isfinite(speech)) speech = 0.0f;
    if (!std::isfinite(music))  music  = 0.0f;
    if (!std::isfinite(bass))   bass   = 0.0f;
    
    // Clampear a [0, 1]
    speech = clampf(speech, 0.0f, 1.0f);
    music  = clampf(music,  0.0f, 1.0f);
    bass   = clampf(bass,   0.0f, 1.0f);

    // === Almacenar scores de clasificación ===
    speechScore.store(speech, std::memory_order_relaxed);
    musicScore.store(music, std::memory_order_relaxed);
    bassScore.store(bass, std::memory_order_relaxed);
    classificationValid.store(true, std::memory_order_relaxed);

    // === Ajustar parámetros DSP según clasificación ===
    
    // Widener: speech ↑ ⇒ estrechar (inteligibilidad)
    //          music  ↑ ⇒ ensanchar (envoltura)
    //          bass   ↑ ⇒ estrechar levemente
    const float base  = 1.0f;
    const float delta = (music - speech) * 0.6f - bass * 0.25f;
    const float tgt   = clampf(base + delta, 0.5f, 1.6f);

    {
        std::lock_guard<std::mutex> lk(mtx);
        targetWidener = tgt;
    }

    // EQ 2-4kHz: boost si speech > threshold
    if (speech > SPEECH_THRESHOLD) {
        eqBoost2k4k.store(2.0f, std::memory_order_relaxed);  // +2dB
    } else {
        eqBoost2k4k.store(0.0f, std::memory_order_relaxed);
    }

    // Exciter: solo <120Hz si bass > threshold
    if (bass > BASS_THRESHOLD) {
        exciterLowOnly.store(true, std::memory_order_relaxed);
    } else {
        exciterLowOnly.store(false, std::memory_order_relaxed);
    }
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
