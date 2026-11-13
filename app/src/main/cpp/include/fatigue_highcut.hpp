#pragma once
// fatigue_highcut.hpp — Acoustic Fatigue Mitigator: high-cut IIR 1er orden.
//
// v2.5.428 — el high-cut de fatiga vive en Ruta A (proceso de la app), no
// en el daemon (muerto, sin Magisk). El slider del panel
// (TinyMlClassifierPanel) fija el cutoff (16000–19500 Hz); el DSP lo aplica
// al final de la cadena de Ruta A, antes del limitador, para suavizar el
// agudo en sesiones largas. 1er orden (RC): alpha = 1 - exp(-2*pi*fc/sr).
// fc >= 20000 Hz = bypass (transparente). El estado (yPrev por canal) se
// conserva entre bloques y entre cambios de cutoff: mover el slider no
// produce clics. Coste: 2 mults por muestra. Fail-closed: cutoff no finito
// o fuera de rango se sanea; nunca se toca el buffer en bypass.

#include <algorithm>
#include <cmath>

namespace ivanna {

class FatigueHighCut {
public:
    // >= este cutoff el filtro es transparente (no se toca el buffer).
    static constexpr float kBypassCutoffHz = 20000.0f;
    static constexpr float kMinCutoffHz = 1000.0f;
    static constexpr float kMaxCutoffHz = 24000.0f;
    // Default del panel: 19500 Hz = prácticamente transparente.
    static constexpr float kDefaultCutoffHz = 19500.0f;

    FatigueHighCut() { setCutoff(kDefaultCutoffHz, 48000.0f); }

    void setCutoff(float fcHz, float sampleRateHz) noexcept {
        fcHz_ = std::isfinite(fcHz)
            ? std::clamp(fcHz, kMinCutoffHz, kMaxCutoffHz)
            : kDefaultCutoffHz;
        srHz_ = (sampleRateHz > 0.0f && std::isfinite(sampleRateHz))
            ? sampleRateHz : 48000.0f;
        alpha_ = 1.0f - std::exp(-2.0f * kPi * fcHz_ / srHz_);
    }

    float cutoffHz() const noexcept { return fcHz_; }
    float alpha() const noexcept { return alpha_; }
    bool bypassed() const noexcept { return fcHz_ >= kBypassCutoffHz; }

    void reset() noexcept { yL_ = 0.0f; yR_ = 0.0f; }

    // Aplica el LPF a ambos canales in-place. No-op en bypass.
    void process(float* l, float* r, int n) noexcept {
        if (bypassed() || n <= 0 || l == nullptr || r == nullptr) return;
        for (int i = 0; i < n; ++i) {
            yL_ += alpha_ * (l[i] - yL_);
            yR_ += alpha_ * (r[i] - yR_);
            l[i] = yL_;
            r[i] = yR_;
        }
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    float fcHz_ = kDefaultCutoffHz;
    float srHz_ = 48000.0f;
    float alpha_ = 0.922f;  // 1 - exp(-2*pi*19500/48000)
    float yL_ = 0.0f, yR_ = 0.0f;
};

}  // namespace ivanna
