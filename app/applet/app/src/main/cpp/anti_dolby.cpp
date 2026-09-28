#include "anti_dolby.h"
#include <algorithm>
#include <cmath>

// ============================================================================
// AntiDolbyState — V3.0 Lock-Free TinyML Implementation
// ============================================================================

namespace {
inline float clampf(float v, float a, float b) {
    return (v < a) ? a : (v > b) ? b : v;
}
} // namespace

AntiDolbyState::AntiDolbyState() {
    widenerMultiplier.store(1.0f, std::memory_order_relaxed);
    targetWidener.store(1.0f, std::memory_order_relaxed);
    smoothedWidener.store(1.0f, std::memory_order_relaxed);
}

void AntiDolbyState::reset() noexcept {
    // Reset de parámetros de suavizado lock-free
    targetWidener.store(1.0f, std::memory_order_relaxed);
    smoothedWidener.store(1.0f, std::memory_order_relaxed);
    
    // Reset de clasificación y parámetros DSP
    speechScore.store(0.0f, std::memory_order_relaxed);
    musicScore.store(0.0f, std::memory_order_relaxed);
    bassScore.store(0.0f, std::memory_order_relaxed);
    classificationValid.store(false, std::memory_order_relaxed);
    
    dominantClass.store(0, std::memory_order_relaxed);
    neuralConfidence.store(0.0f, std::memory_order_relaxed);
    sceneEnergy.store(0.0f, std::memory_order_relaxed);
    spatialSpreadMul.store(1.0f, std::memory_order_relaxed);
    bassExciterLevel.store(0.0f, std::memory_order_relaxed);

    widenerMultiplier.store(1.0f, std::memory_order_release);
    eqBoost2k4k.store(0.0f, std::memory_order_relaxed);
    exciterLowOnly.store(false, std::memory_order_relaxed);
    
    frameCounter.store(0, std::memory_order_relaxed);
}

void AntiDolbyState::updateFromClassification(float speech, float music, float bass) noexcept {
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
    const float base  = 1.0f;
    const float delta = (music - speech) * 0.6f - bass * 0.25f;
    const float tgt   = clampf(base + delta, 0.5f, 1.6f);

    targetWidener.store(tgt, std::memory_order_relaxed);

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

void AntiDolbyState::updateFromNeuralContext(uint8_t contextClass, float confidence, float energy) noexcept {
    dominantClass.store(contextClass, std::memory_order_relaxed);
    neuralConfidence.store(confidence, std::memory_order_relaxed);
    sceneEnergy.store(energy, std::memory_order_relaxed);
    classificationValid.store(true, std::memory_order_relaxed);

    float tgtWidener = 1.0f;
    float tgtEqBoost = 0.0f;
    bool  lowExciter = false;
    float spreadMul  = 1.0f;
    float exciterLvl = 0.0f;

    switch (static_cast<AcousticScene>(contextClass)) {
        case AcousticScene::VOICE:
            // Enfocar centro estéreo para inteligibilidad suprema de voces
            tgtWidener = 0.75f;
            tgtEqBoost = 2.5f; // Claridad en frecuencias formantes
            lowExciter = false;
            spreadMul  = 0.90f;
            exciterLvl = 0.0f;
            speechScore.store(confidence, std::memory_order_relaxed);
            musicScore.store(0.1f, std::memory_order_relaxed);
            break;

        case AcousticScene::MUSIC:
            // Apertura de campo sonoro audiófilo transparente
            tgtWidener = 1.35f;
            tgtEqBoost = 0.0f; // Respuesta de fase lineal pura
            lowExciter = true;
            spreadMul  = 1.25f; // WFS wavefield widening
            exciterLvl = 0.35f;
            musicScore.store(confidence, std::memory_order_relaxed);
            speechScore.store(0.1f, std::memory_order_relaxed);
            break;

        case AcousticScene::MOVIE:
            // Espacialidad envolvente y sub-graves cinematográficos
            tgtWidener = 1.25f;
            tgtEqBoost = 1.5f;
            lowExciter = true;
            spreadMul  = 1.30f;
            exciterLvl = 0.75f;
            break;

        case AcousticScene::GAME:
            // Localización espacial binaural de ultra-baja latencia
            tgtWidener = 1.10f;
            tgtEqBoost = 0.8f;
            lowExciter = false;
            spreadMul  = 1.15f;
            exciterLvl = 0.20f;
            break;

        case AcousticScene::AMBIENT:
            // Atmósfera difusa envolvente
            tgtWidener = 1.40f;
            tgtEqBoost = 0.0f;
            lowExciter = false;
            spreadMul  = 1.35f;
            exciterLvl = 0.10f;
            break;

        case AcousticScene::UNKNOWN:
        default:
            tgtWidener = 1.0f;
            tgtEqBoost = 0.0f;
            lowExciter = false;
            spreadMul  = 1.0f;
            exciterLvl = 0.0f;
            break;
    }

    targetWidener.store(tgtWidener, std::memory_order_relaxed);
    eqBoost2k4k.store(tgtEqBoost, std::memory_order_relaxed);
    exciterLowOnly.store(lowExciter, std::memory_order_relaxed);
    spatialSpreadMul.store(spreadMul, std::memory_order_relaxed);
    bassExciterLevel.store(exciterLvl, std::memory_order_relaxed);
}

void AntiDolbyState::tick(float dt) noexcept {
    // Robustez: dt fuera de rango ⇒ paso conservador (1 muestra a 48 kHz).
    if (!std::isfinite(dt) || dt <= 0.0f) dt = 1.0f / 96000.0f;
    if (dt > 0.1f)                        dt = 0.1f;   // no permitir >100 ms

    const float curTgt = targetWidener.load(std::memory_order_relaxed);
    float curSmooth    = smoothedWidener.load(std::memory_order_relaxed);
    const float aTau   = attackTau.load(std::memory_order_relaxed);
    const float rTau   = releaseTau.load(std::memory_order_relaxed);

    const float tau   = (curTgt < curSmooth) ? aTau : rTau;
    const float alpha = 1.0f - std::exp(-dt / (tau > 1e-4f ? tau : 1e-4f));
    curSmooth        += alpha * (curTgt - curSmooth);

    smoothedWidener.store(curSmooth, std::memory_order_relaxed);
    widenerMultiplier.store(curSmooth, std::memory_order_release);
}

void AntiDolbyState::setAttackTau(float seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return;
    attackTau.store(seconds, std::memory_order_relaxed);
}

void AntiDolbyState::setReleaseTau(float seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return;
    releaseTau.store(seconds, std::memory_order_relaxed);
}

void AntiDolbyState::setAntiDolbyIntensity(float intensity) noexcept {
    if (!std::isfinite(intensity)) intensity = 0.0f;
    antiDolbyIntensity.store(clampf(intensity, 0.0f, 1.0f), std::memory_order_relaxed);
}

