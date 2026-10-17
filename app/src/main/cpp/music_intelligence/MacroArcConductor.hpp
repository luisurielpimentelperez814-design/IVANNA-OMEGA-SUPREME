// MacroArcConductor.hpp — Arco narrativo de la canción (escala de SEGUNDOS)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// QUÉ FALTABA: ExcitationConductor anticipa el PRÓXIMO GOLPE (5–10 ms) y abre el campo
// un instante antes. Pero no sabe en qué PARTE de la canción está: no distingue una
// subida de tensión de un estribillo ya instalado, ni el hueco previo a un drop del
// silencio de un tema tranquilo. Este módulo mide esa forma a escala de segundos y la
// convierte en tres gestos que el cuerpo reconoce:
//
//   1. SUBIDA  (Build)   — la excitación (relativa al máximo propio del tema) crece con
//                          ataque lento (1.5 s) y se PRONOSTICA a
//                          0.5 s por regresión lineal sobre 1 s: el campo empieza a abrirse
//                          durante la subida, no cuando ya llegó arriba.
//   2. HUECO   (tension) — cuando el pulso (sub-grave 20–80 Hz) se retira de golpe tras
//                          haber estado presente, sube la tensión y el campo se recoge un
//                          poco: el contraste que prepara el impacto.
//   3. IMPACTO (impact)  — cuando el pulso vuelve tras un hueco de >= 250 ms se dispara UN
//                          pulso de impacto (envolvente de ~350 ms) que refuerza el sub y
//                          el golpe háptico justo en el retorno.
//
// La excitación tiene release lento (4 s): el cuerpo no se deshabitúa en cada compás.
//
// REGLAS DE LA CASA:
//   · Apagado por defecto y profundidad 0: con g = depth·enabled <= 1e-4 modulate() es
//     identidad exacta (copia de la base) — mismo contrato que ExcitationConductor.
//   · Solo MODULA objetivos de escena (sub, ancho, aire, elevación, HRTF) con topes
//     modestos; NO toca ganancia de salida ni SafetyLimiter. El techo de aire lo impone
//     SceneTargetBus (maxExcWetCeiling). La háptica sigue gobernada por el dosímetro.
//   · RT-safe: cero malloc, cero locks, cero excepciones; estado POD de tamaño fijo.
//     Entradas no finitas se tratan como 0 y no envenenan el estado.
//
// LÍMITE HONESTO: los pesos de la excitación y los umbrales del patrón de drop se
// verifican con características sintéticas (tests/test_macro_arc_conductor.cpp); el
// ajuste fino con música real y con el Genezi se hace de oído en el dispositivo.
#pragma once

#include "SceneTargetBus.hpp"
#include "MusicFeatureExtractor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ivanna { namespace ime {

enum class ArcPhase : uint8_t { Calm = 0, Build = 1, Peak = 2, Release = 3 };

struct alignas(64) MacroArcOutput {
    float arousal      = 0.0f;  // curva de excitación suavizada 0..1
    float anticipation = 0.0f;  // 0..1, cuánto se espera que siga subiendo
    float tension      = 0.0f;  // 0..1, hueco previo al drop
    float impact       = 0.0f;  // 0..1, envolvente del impacto (decae ~350 ms)
    float openness     = 0.0f;  // 0..1, apertura lenta del campo (con límite de velocidad)
    bool  impactPulse  = false; // true SOLO en el tick en que el pulso regresa
    ArcPhase phase     = ArcPhase::Calm;
};

class alignas(64) MacroArcConductor {
public:
    static constexpr float kTickMs        = 50.0f;   // 20 Hz
    static constexpr int   kHistory       = 20;      // 1 s de curva rápida
    static constexpr float kAttackSec     = 1.5f;
    static constexpr float kReleaseSec    = 4.0f;
    static constexpr float kFastTauSec    = 0.4f;
    static constexpr float kHorizonSec    = 0.5f;
    static constexpr float kImpactDecaySec = 0.35f;
    static constexpr float kOpenUpPerSec   = 0.5f;
    static constexpr float kOpenDownPerSec = 1.0f;
    static constexpr int   kMaxHoldTicks   = 320;   // 16 s a 20 Hz

    MacroArcConductor() noexcept { reset(); }

    void prepare(float sampleRate, int maxBlock) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        maxBlock_ = (maxBlock > 0) ? maxBlock : 256;
        const float dt = kTickMs * 0.001f;
        aAtk_  = 1.0f - std::exp(-dt / kAttackSec);
        aRel_  = 1.0f - std::exp(-dt / kReleaseSec);
        aFast_ = 1.0f - std::exp(-dt / kFastTauSec);
        tensionDecay_ = std::exp(-dt / 0.6f);
        peakDecay_    = std::exp(-dt / 3.0f);
        aAnt_         = 1.0f - std::exp(-dt / 0.3f);
        reset();
    }

    void reset() noexcept {
        if (aAtk_ == 0.0f) {  // construcción sin prepare(): coeficientes por defecto a 20 Hz
            const float dt = kTickMs * 0.001f;
            aAtk_  = 1.0f - std::exp(-dt / kAttackSec);
            aRel_  = 1.0f - std::exp(-dt / kReleaseSec);
            aFast_ = 1.0f - std::exp(-dt / kFastTauSec);
            tensionDecay_ = std::exp(-dt / 0.6f);
            peakDecay_    = std::exp(-dt / 3.0f);
            aAnt_         = 1.0f - std::exp(-dt / 0.3f);
        }
        accMs_ = 0.0f; accDrive_ = 0.0f; accPulse_ = 0.0f;
        arousal_ = fast_ = 0.0f; ref_ = 0.60f;
        ring_.fill(0.0f); ringPos_ = 0; filled_ = 0;
        tension_ = 0.0f; impactEnv_ = 0.0f; recentPulsePeak_ = 0.0f; dipTicks_ = 0; anticipationS_ = 0.0f;
        phase_ = ArcPhase::Calm;
        out_ = MacroArcOutput{};
        slewSub_ = 0.0f; slewSpread_ = 0.5f; slewAir_ = 0.0f; slewElev_ = 0.0f; slewHrtf_ = 0.5f;
        slewSeeded_ = false;
        blockMs_ = 5.0f;
    }

    // ── Controles atómicos (UI / JNI / decisión adaptativa) ──────────────────
    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
    void setDepth(float d) noexcept {
        depth_.store(std::isfinite(d) ? std::clamp(d, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    }
    float depth() const noexcept { return depth_.load(std::memory_order_relaxed); }

    // ── Avance por bloque ────────────────────────────────────────────────────
    // Acumula hasta completar un tick de 50 ms y entonces actualiza la curva.
    void advance(const MusicFeatures& f, float blockMs) noexcept {
        const float dt = (std::isfinite(blockMs) && blockMs > 0.0f) ? blockMs
                       : (static_cast<float>(maxBlock_) / sr_) * 1000.0f;
        blockMs_ = dt;
        if (!enabled_.load(std::memory_order_relaxed)) {
            if (out_.arousal != 0.0f || out_.openness != 0.0f || out_.impact != 0.0f
                || out_.tension != 0.0f || out_.phase != ArcPhase::Calm) {
                reset();   // apagado: estado limpio, sin residuos al reencender
            }
            return;
        }
        // La envolvente de impacto decae por bloque (no por tick) para que sea suave.
        impactEnv_ *= std::exp(-dt / (kImpactDecaySec * 1000.0f));
        if (impactEnv_ < 1e-4f) impactEnv_ = 0.0f;

        // PESOS CALIBRADOS CON EL EXTRACTOR REAL (no con suposiciones): sobre un tema de
        // bombo+bajo con un hueco de solo colchón, subBandRatio (20–80 Hz) cae 0.59 -> 0.04,
        // mientras bassRatio apenas cambia (0.90 -> 0.53) y transientRate es ruidoso (sube
        // en el hueco). Por eso el pulso se apoya en el sub y NO en bassRatio/transientRate.
        const float level = clamp01(std::sqrt(std::max(sane(f.rms), 0.0f)) * 1.6f);
        const float sub   = clamp01(sane(f.subBandRatio));
        const float body  = clamp01(sane(f.bodyBandRatio));
        const float bright = clamp01((sane(f.presenceRatio) + sane(f.airRatio)) * 1.6f);
        const float trans = clamp01(sane(f.transientRate) * 3.0f);
        // Excitación global: nivel, peso grave, brillo y golpes (estos últimos con poco peso).
        const float drive = 0.50f * level + 0.30f * clamp01(sub + 0.5f * body)
                          + 0.10f * bright + 0.10f * trans;
        // "Pulso": el hueco del drop se ve en el sub aunque el nivel general apenas baje
        // (en música masterizada casi nunca baja).
        const float pulse = 0.85f * sub + 0.15f * level;

        accDrive_ += drive * dt; accPulse_ += pulse * dt; accMs_ += dt;
        if (accMs_ >= kTickMs) {
            tick(accDrive_ / accMs_, accPulse_ / accMs_);
            accMs_ = 0.0f; accDrive_ = 0.0f; accPulse_ = 0.0f;
        }
        out_.impact = impactEnv_;
    }

    MacroArcOutput output() const noexcept { return out_; }

    // Refuerzo de golpe (0..0.5) para la háptica: solo durante el impacto y escalado por
    // la profundidad. La háptica sigue pasando por el techo y por la atenuación de dosis.
    float hapticPunchBoost() const noexcept {
        if (!enabled_.load(std::memory_order_relaxed)) return 0.0f;
        return std::clamp(0.5f * impactEnv_ * depth_.load(std::memory_order_relaxed), 0.0f, 0.5f);
    }

    // ── Modulación de la escena ──────────────────────────────────────────────
    // base = objetivos ya adelantados por ExcitationConductor::modulate(). Con g ~ 0
    // devuelve copia literal (identidad exacta) y deja el suavizado siguiendo a la base.
    SceneApply modulate(const SceneApply& base, float maxExcWetCeiling = 0.15f) const noexcept {
        SceneApply out = base;
        const float g = enabled_.load(std::memory_order_relaxed)
                      ? depth_.load(std::memory_order_relaxed) : 0.0f;
        if (g <= 1.0e-4f) {
            seed(base);
            return out;
        }
        if (!slewSeeded_) seed(base);

        const float open = out_.openness, tens = out_.tension, imp = impactEnv_;
        const float ceil = std::max(maxExcWetCeiling, base.t.airHolography);

        const float subT  = std::clamp(base.t.subPunch + 0.20f * g * imp, 0.0f, 0.85f);
        const float sprT  = std::clamp(base.t.wfsSpread * (1.0f + 0.20f * g * open - 0.10f * g * tens),
                                       0.10f, 1.0f);
        const float airT  = std::clamp(base.t.airHolography + 0.15f * g * open, 0.0f, ceil);
        const float elevT = std::clamp(base.t.stageElevation + 0.05f * g * open, 0.0f, 0.65f);
        const float hrtfT = std::clamp(base.t.hrtfDepth + 0.06f * g * open, 0.0f, 1.0f);

        // Suavizado: el sub (impacto) responde en ~30 ms; el resto asienta en ~120 ms.
        const float aFast = 1.0f - std::exp(-blockMs_ / 30.0f);
        const float aSlow = 1.0f - std::exp(-blockMs_ / 120.0f);
        slewSub_    += aFast * (subT  - slewSub_);
        slewSpread_ += aSlow * (sprT  - slewSpread_);
        slewAir_    += aSlow * (airT  - slewAir_);
        slewElev_   += aSlow * (elevT - slewElev_);
        slewHrtf_   += aSlow * (hrtfT - slewHrtf_);

        out.t.subPunch       = slewSub_;
        out.t.wfsSpread      = slewSpread_;
        out.t.airHolography  = slewAir_;
        out.t.stageElevation = slewElev_;
        out.t.hrtfDepth      = slewHrtf_;
        // targetIacc, eqTilt, dinámica, calidez: intactos (el arco no estrecha la imagen
        // interaural objetivo ni toca la dinámica).
        return out;
    }

private:
    static float sane(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
    static float clamp01(float v) noexcept { return std::min(1.0f, std::max(0.0f, v)); }
    static float smooth(float x) noexcept { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }

    void seed(const SceneApply& b) const noexcept {
        slewSub_ = b.t.subPunch;      slewSpread_ = b.t.wfsSpread;
        slewAir_ = b.t.airHolography; slewElev_   = b.t.stageElevation;
        slewHrtf_ = b.t.hrtfDepth;
        slewSeeded_ = true;
    }

    float slewStep(float cur, float target, float upPerSec, float downPerSec) const noexcept {
        const float dt = kTickMs * 0.001f;
        if (target > cur) return std::min(target, cur + upPerSec * dt);
        return std::max(target, cur - downPerSec * dt);
    }

    // Pendiente (1/s) por regresión lineal de la curva rápida sobre los ticks disponibles.
    float regressionSlopePerSec() const noexcept {
        const int m = filled_;
        if (m < 6) return 0.0f;
        float sx = 0, sy = 0, sxy = 0, sxx = 0;
        for (int i = 0; i < m; ++i) {
            const int idx = (ringPos_ - m + i + 2 * kHistory) % kHistory;
            const float y = ring_[static_cast<size_t>(idx)];
            const float x = static_cast<float>(i);
            sx += x; sy += y; sxy += x * y; sxx += x * x;
        }
        const float n = static_cast<float>(m);
        const float den = n * sxx - sx * sx;
        if (den <= 1e-9f) return 0.0f;
        return ((n * sxy - sx * sy) / den) * (1000.0f / kTickMs);
    }

    void tick(float drive, float pulse) noexcept {
        arousal_ += (drive - arousal_) * (drive > arousal_ ? aAtk_ : aRel_);
        fast_    += (drive - fast_) * aFast_;
        // Referencia propia del tema: sube despacio (15 s) hacia su máximo y decae muy
        // despacio (90 s), con suelo 0.40. Así "Peak" es relativo al propio tema y el arco
        // funciona igual con una masterización suave o aplastada.
        const float dtS = kTickMs * 0.001f;
        const float tau = (arousal_ > ref_) ? 15.0f : 90.0f;
        ref_ = std::max(0.40f, ref_ + (arousal_ - ref_) * (1.0f - std::exp(-dtS / tau)));
        const float an = clamp01(arousal_ / ref_);
        const float fn = clamp01(fast_ / ref_);
        ring_[static_cast<size_t>(ringPos_)] = fn;
        ringPos_ = (ringPos_ + 1) % kHistory;
        if (filled_ < kHistory) ++filled_;
        const float slope = regressionSlopePerSec();

        // ── Patrón del drop sobre el PULSO ──
        bool pulseNow = false;
        // La referencia del pulso NO decae mientras dura el hueco (hasta 16 s): si decayera,
        // un breakdown largo dejaría de verse como hueco a mitad y el impacto se perdería.
        const bool holding = (dipTicks_ > 0) && (dipTicks_ < kMaxHoldTicks);
        recentPulsePeak_ = std::max(pulse, holding ? recentPulsePeak_ : recentPulsePeak_ * peakDecay_);
        const bool dipped = (recentPulsePeak_ > 0.30f) && (pulse < 0.30f * recentPulsePeak_);
        if (dipped) {
            ++dipTicks_;
            tension_ = std::min(1.0f, tension_ + kTickMs / 400.0f);   // sube en ~0.4 s
        } else {
            tension_ *= tensionDecay_;
            if (dipTicks_ >= 5 && pulse > 0.60f * recentPulsePeak_ && pulse > 0.25f) {
                pulseNow = true;                  // el pulso volvió tras >= 250 ms de hueco
                impactEnv_ = 1.0f;
                recentPulsePeak_ = pulse;         // reinicia la referencia tras el impacto
            }
            dipTicks_ = 0;
        }

        updatePhase(an, slope);

        const float forecast = clamp01(an + std::max(slope, 0.0f) * kHorizonSec);
        float anticipationRaw = clamp01((forecast - an) / 0.12f);
        if (phase_ != ArcPhase::Build) anticipationRaw *= 0.4f;
        anticipationS_ += aAnt_ * (anticipationRaw - anticipationS_);   // ~0.3 s: sin escalón en Build->Peak
        const float anticipation = anticipationS_;

        const float openTarget = clamp01(0.80f * smooth(an) + 0.20f * anticipation);
        out_.openness     = slewStep(out_.openness, openTarget, kOpenUpPerSec, kOpenDownPerSec);
        out_.arousal      = an;
        out_.anticipation = anticipation;
        out_.tension      = tension_;
        out_.impactPulse  = pulseNow;
        out_.phase        = phase_;
    }

    void updatePhase(float a, float slope) noexcept {
        switch (phase_) {
            case ArcPhase::Calm:
                if (slope > 0.06f && a > 0.12f) phase_ = ArcPhase::Build;
                else if (a >= 0.70f) phase_ = ArcPhase::Peak;
                break;
            case ArcPhase::Build:
                if (a >= 0.70f && slope < 0.10f) phase_ = ArcPhase::Peak;
                else if (slope < -0.08f) phase_ = ArcPhase::Release;
                else if (a < 0.10f) phase_ = ArcPhase::Calm;
                break;
            case ArcPhase::Peak:
                if (slope < -0.10f || a < 0.55f) phase_ = ArcPhase::Release;
                break;
            case ArcPhase::Release:
                if (slope > 0.06f && a > 0.12f) phase_ = ArcPhase::Build;
                else if (a >= 0.70f && std::fabs(slope) < 0.10f) phase_ = ArcPhase::Peak;
                else if (a < 0.15f) phase_ = ArcPhase::Calm;
                break;
        }
    }

    float sr_ = 48000.0f;
    int   maxBlock_ = 256;
    float blockMs_ = 5.0f;
    float aAtk_ = 0.0f, aRel_ = 0.0f, aFast_ = 0.0f, tensionDecay_ = 0.0f, peakDecay_ = 0.0f;
    float accMs_ = 0.0f, accDrive_ = 0.0f, accPulse_ = 0.0f;
    float ref_ = 0.60f, anticipationS_ = 0.0f, aAnt_ = 0.0f;
    float arousal_ = 0.0f, fast_ = 0.0f, tension_ = 0.0f, impactEnv_ = 0.0f, recentPulsePeak_ = 0.0f;
    int   dipTicks_ = 0;
    std::array<float, kHistory> ring_{};
    int   ringPos_ = 0, filled_ = 0;
    ArcPhase phase_ = ArcPhase::Calm;
    MacroArcOutput out_{};

    mutable float slewSub_ = 0.0f, slewSpread_ = 0.5f, slewAir_ = 0.0f, slewElev_ = 0.0f, slewHrtf_ = 0.5f;
    mutable bool  slewSeeded_ = false;

    std::atomic<bool>  enabled_{false};  // apagado por defecto (filosofía del repo)
    std::atomic<float> depth_{0.0f};
};

}} // namespace ivanna::ime
