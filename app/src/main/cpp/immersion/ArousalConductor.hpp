// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================================
 * IVANNA OMEGA SUPREME — ArousalConductor (immersion/)
 * ============================================================================
 *
 * Conductor de excitación ANTICIPATORIO. Lee rasgos lentos de la música
 * (densidad de golpes, energía de graves, brillo, nivel) a la tasa del hilo de
 * control (~20 Hz) y publica objetivos suaves para la capa de inmersión:
 * apertura del campo espacial, ganancia háptica y de armónicos, tensión.
 *
 * Qué hace de distinto a un simple seguidor de energía:
 *   - Mantiene una "curva de excitación" con ataque lento (~1.5 s) y release
 *     más lento (~4 s): el cuerpo no se deshabitúa en cada compás.
 *   - Estima la pendiente de la curva por regresión lineal sobre 1 s y
 *     PRONOSTICA hacia dónde va (horizonte 0.5 s): en una subida, el campo se
 *     abre ANTES de que llegue el golpe, no después.
 *   - Detecta el patrón "caída de energía + retorno brusco" (el silencio previo
 *     al drop): sube la tensión durante el hueco y dispara un pulso de impacto
 *     de un solo tick cuando la energía regresa.
 *   - Cada objetivo tiene límite de velocidad (slew): nunca hay saltos que
 *     suenen o se sientan como click.
 *
 * Garantías:
 *   - Solo escribe objetivos en [0, 1]; NO toca la ganancia de salida ni el
 *     SafetyLimiter. Los techos (maxOpenness/maxHaptic/maxHarmonic) los puede
 *     bajar quien lo integre (p.ej. el dosímetro de exposición).
 *   - Sin malloc, sin hilos, sin excepciones: estado POD de tamaño fijo.
 *   - Entradas no finitas se tratan como 0 y no envenenan el estado.
 *
 * Estado: módulo nuevo, probado en host (tests/test_arousal_conductor.cpp).
 * Lo consume ImmersionCore; el cableado al audio thread es una fase aparte.
 * ============================================================================
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ivanna::immersion {

struct ArousalFeatures {
    float onsetRate  = 0.0f;  // golpes por segundo (se satura en 8)
    float lowEnergy  = 0.0f;  // 0..1, energía relativa sub+bajo
    float brightness = 0.0f;  // 0..1, centroide espectral normalizado
    float level      = 0.0f;  // 0..1, RMS lineal
};

enum class ArousalPhase : uint8_t { Calm = 0, Build = 1, Peak = 2, Release = 3 };

struct ImmersionTargets {
    float arousal       = 0.0f;  // curva de excitación suavizada
    float anticipation  = 0.0f;  // 0..1, cuánto se espera que suba
    float tension       = 0.0f;  // 0..1, hueco previo al drop
    float impactPulse   = 0.0f;  // 1.0 durante un solo tick al retorno del drop
    float fieldOpenness = 0.0f;  // 0..1, apertura sugerida del campo espacial
    float hapticGain    = 0.0f;  // 0..1, intensidad háptica sugerida
    float harmonicGain  = 0.0f;  // 0..1, riqueza armónica sugerida
    ArousalPhase phase  = ArousalPhase::Calm;
};

class ArousalConductor {
public:
    static constexpr int   kHistory        = 20;     // 1 s a 20 Hz
    static constexpr float kAttackSec      = 1.5f;
    static constexpr float kReleaseSec     = 4.0f;
    static constexpr float kFastTauSec     = 0.4f;
    static constexpr float kHorizonSec     = 0.5f;
    static constexpr float kTensionDecaySec = 0.6f;

    void prepare(float controlRateHz = 20.0f) noexcept {
        rate_ = (std::isfinite(controlRateHz) && controlRateHz >= 5.0f && controlRateHz <= 200.0f)
                    ? controlRateHz : 20.0f;
        dt_ = 1.0f / rate_;
        aAtk_ = 1.0f - std::exp(-dt_ / kAttackSec);
        aRel_ = 1.0f - std::exp(-dt_ / kReleaseSec);
        aFast_ = 1.0f - std::exp(-dt_ / kFastTauSec);
        tensionDecay_ = std::exp(-dt_ / kTensionDecaySec);
        reset();
    }

    void reset() noexcept {
        arousal_ = fast_ = 0.0f;
        ring_.fill(0.0f);
        ringPos_ = 0;
        filled_ = 0;
        phase_ = ArousalPhase::Calm;
        tension_ = 0.0f;
        recentPeak_ = 0.0f;
        dipTicks_ = 0;
        out_ = ImmersionTargets{};
    }

    /** Techos que el integrador puede bajar (dosis auditiva, modo ahorro). */
    void setCeilings(float openness, float haptic, float harmonic) noexcept {
        maxOpenness_ = clamp01(openness);
        maxHaptic_   = clamp01(haptic);
        maxHarmonic_ = clamp01(harmonic);
    }

    ImmersionTargets update(const ArousalFeatures& f) noexcept {
        const float onset  = clamp01(sane(f.onsetRate) / 8.0f);
        const float low    = clamp01(sane(f.lowEnergy));
        const float bright = clamp01(sane(f.brightness));
        const float level  = clamp01(sane(f.level));
        const float drive  = clamp01(0.35f * onset + 0.30f * low + 0.15f * bright +
                                     0.20f * std::sqrt(level));

        // Curva de excitación: ataque lento, release más lento.
        arousal_ += (drive - arousal_) * (drive > arousal_ ? aAtk_ : aRel_);
        fast_    += (drive - fast_) * aFast_;

        ring_[ringPos_] = fast_;
        ringPos_ = (ringPos_ + 1) % kHistory;
        if (filled_ < kHistory) ++filled_;

        const float slope = regressionSlopePerSec();  // 1/s, de la curva rápida

        updateDropPattern(drive);
        updatePhase(slope);

        // Pronóstico a 0.5 s: solo cuenta si la tendencia es de subida.
        const float forecast = clamp01(arousal_ + std::max(slope, 0.0f) * kHorizonSec);
        // Un avance previsto de 0.12 en 0.5 s ya cuenta como anticipación plena.
        float anticipation = clamp01((forecast - arousal_) / 0.12f);
        if (phase_ != ArousalPhase::Build) anticipation *= 0.4f;

        const float a = arousal_;
        const float openTarget = clamp01(0.80f * smooth(a) + 0.20f * anticipation);
        const float hapTarget  = clamp01(0.70f * std::pow(a, 1.2f) + 0.30f * impact_);
        const float harmTarget = clamp01(0.40f + 0.60f * a);

        out_.arousal      = a;
        out_.anticipation = anticipation;
        out_.tension      = tension_;
        out_.impactPulse  = impact_;
        out_.phase        = phase_;
        out_.fieldOpenness = slew(out_.fieldOpenness, std::min(openTarget, maxOpenness_), 0.5f, 1.0f);
        out_.hapticGain    = slew(out_.hapticGain,    std::min(hapTarget,  maxHaptic_),   4.0f, 2.0f);
        out_.harmonicGain  = slew(out_.harmonicGain,  std::min(harmTarget, maxHarmonic_), 0.5f, 1.0f);
        return out_;
    }

    const ImmersionTargets& last() const noexcept { return out_; }

    // Límites de velocidad (unidades/s) — expuestos para las pruebas.
    static constexpr float kOpenUpPerSec   = 0.5f,  kOpenDownPerSec   = 1.0f;
    static constexpr float kHapticUpPerSec = 4.0f,  kHapticDownPerSec = 2.0f;
    static constexpr float kHarmUpPerSec   = 0.5f,  kHarmDownPerSec   = 1.0f;

private:
    static float sane(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
    static float clamp01(float v) noexcept { return std::min(1.0f, std::max(0.0f, v)); }
    static float smooth(float x) noexcept { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }

    float slew(float cur, float target, float upPerSec, float downPerSec) const noexcept {
        const float up = upPerSec * dt_, down = downPerSec * dt_;
        if (target > cur) return std::min(target, cur + up);
        return std::max(target, cur - down);
    }

    // Pendiente por regresión lineal sobre los últimos (hasta) kHistory ticks.
    float regressionSlopePerSec() const noexcept {
        const int m = filled_;
        if (m < 6) return 0.0f;
        float sx = 0, sy = 0, sxy = 0, sxx = 0;
        for (int i = 0; i < m; ++i) {
            // i = 0 es la muestra más antigua de las m disponibles.
            const int idx = (ringPos_ - m + i + 2 * kHistory) % kHistory;
            const float y = ring_[idx];
            const float x = static_cast<float>(i);
            sx += x; sy += y; sxy += x * y; sxx += x * x;
        }
        const float n = static_cast<float>(m);
        const float den = n * sxx - sx * sx;
        if (den <= 1e-9f) return 0.0f;
        return ((n * sxy - sx * sy) / den) * rate_;
    }

    // Patrón del drop: caída fuerte de la energía instantánea frente al pico
    // reciente (tensión) y retorno brusco (pulso de impacto de un tick).
    void updateDropPattern(float drive) noexcept {
        impact_ = 0.0f;
        recentPeak_ = std::max(drive, recentPeak_ * peakDecay());
        const bool dipped = (recentPeak_ > 0.45f) && (drive < 0.25f * recentPeak_);
        if (dipped) {
            ++dipTicks_;
            tension_ = std::min(1.0f, tension_ + dt_ / 0.4f);  // sube en ~0.4 s
        } else {
            tension_ *= tensionDecay_;
            if (dipTicks_ >= static_cast<int>(0.25f * rate_) && drive > 0.55f * recentPeak_ && drive > 0.35f) {
                impact_ = 1.0f;
                recentPeak_ = drive;  // reinicia la referencia tras el impacto
            }
            dipTicks_ = 0;
        }
    }
    float peakDecay() const noexcept { return std::exp(-dt_ / 3.0f); }

    void updatePhase(float slope) noexcept {
        const float a = arousal_;
        switch (phase_) {
            case ArousalPhase::Calm:
                if (slope > 0.06f && a > 0.12f) phase_ = ArousalPhase::Build;
                else if (a >= 0.70f) phase_ = ArousalPhase::Peak;
                break;
            case ArousalPhase::Build:
                if (a >= 0.70f && slope < 0.10f) phase_ = ArousalPhase::Peak;
                else if (slope < -0.08f) phase_ = ArousalPhase::Release;
                else if (a < 0.10f) phase_ = ArousalPhase::Calm;
                break;
            case ArousalPhase::Peak:
                if (slope < -0.10f || a < 0.55f) phase_ = ArousalPhase::Release;
                break;
            case ArousalPhase::Release:
                if (slope > 0.06f && a > 0.12f) phase_ = ArousalPhase::Build;
                else if (a >= 0.70f && std::fabs(slope) < 0.10f) phase_ = ArousalPhase::Peak;
                else if (a < 0.15f) phase_ = ArousalPhase::Calm;
                break;
        }
    }

    float rate_ = 20.0f, dt_ = 0.05f;
    float aAtk_ = 0.0f, aRel_ = 0.0f, aFast_ = 0.0f, tensionDecay_ = 0.0f;
    float arousal_ = 0.0f, fast_ = 0.0f, tension_ = 0.0f, impact_ = 0.0f, recentPeak_ = 0.0f;
    int   dipTicks_ = 0;
    std::array<float, kHistory> ring_{};
    int   ringPos_ = 0, filled_ = 0;
    ArousalPhase phase_ = ArousalPhase::Calm;
    float maxOpenness_ = 1.0f, maxHaptic_ = 1.0f, maxHarmonic_ = 1.0f;
    ImmersionTargets out_{};
};

}  // namespace ivanna::immersion
