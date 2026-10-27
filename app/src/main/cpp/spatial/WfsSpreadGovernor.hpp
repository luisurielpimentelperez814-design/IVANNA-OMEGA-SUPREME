#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// WfsSpreadGovernor.hpp — acondicionamiento del "spread" de Wave Field Synthesis.
//
// POR QUÉ EXISTE
// El slider WFS escribía g_wfs_spread y IvannaFusionCore lo usaba en crudo:
//     const float spread = g_wfs_spread.load() * m_wfsCoordScale_;
//     m_wfs.setObject(0, -0.75f * spread, 1.5f, 1.0f);
//     m_wfs.setObject(1,  0.75f * spread, 1.5f, 1.0f);
// Eso tiene tres defectos medibles:
//   1. Sin suavizado: cada gesto del dedo salta de golpe la posición de las dos
//      fuentes virtuales en un bloque (10 ms) → zipper/click en el campo.
//   2. Sin cota física: con spread·escala > 1 las fuentes salen del radio del
//      array (kArrayRadiusM = 1.5 m) y el renderizado pierde coherencia: el
//      slider "deja de hacer efecto" porque satura fuera de la apertura.
//   3. Sin defensa numérica: un NaN de la UI se propagaba a setObject().
//
// Este gobernador aplica, en este orden: saneado → recorte a la apertura útil →
// suavizado de un polo con coeficiente dependiente del bloque. Es puro y se
// prueba en host (tests/test_wfs_spread_governor.cpp).
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <cmath>

namespace ivanna::spatial {

class WfsSpreadGovernor {
public:
    // Apertura útil en metros: fracción del radio del array que puede ocupar
    // cada fuente. 0.90 deja margen para el frente de onda antes del borde.
    static constexpr float kArrayRadiusM   = 1.5f;
    static constexpr float kMaxApertureFrac = 0.90f;
    static constexpr float kBaseOffsetM     = 0.75f;  // separación nominal L/R
    static constexpr float kMinSpread       = 0.0f;
    static constexpr float kMaxSpread       = 2.0f;

    struct Output {
        float spread = 1.0f;       // valor saneado y suavizado (adimensional)
        float offsetM = 0.75f;     // apertura efectiva por fuente, en metros
        bool  clamped = false;     // true si hubo recorte (para telemetría)
        bool  sanitized = false;   // true si entró NaN/inf y se sustituyó
    };

    // `blocksPerSecond` permite fijar el tiempo de asentamiento en segundos.
    void configure(float blocksPerSecond, float settleSeconds = 0.12f) noexcept {
        const float bps = (std::isfinite(blocksPerSecond) && blocksPerSecond > 0.f)
                              ? blocksPerSecond : 100.f;
        const float tau = (std::isfinite(settleSeconds) && settleSeconds > 0.f)
                              ? settleSeconds : 0.12f;
        // y[n] = y[n-1] + a·(x - y[n-1]);  a = 1 - exp(-1/(tau·bps))
        const float a = 1.0f - std::exp(-1.0f / (tau * bps));
        coeff_ = std::clamp(a, 1e-4f, 1.0f);
    }

    // reset() fija el punto de partida Y deja el suavizado activo: si primed_
    // quedara en false, el primer process() saltaria directo al objetivo y el
    // gesto del dedo volveria a oirse como escalon.
    void reset(float spread = 1.0f) noexcept {
        state_ = sanitize(spread);
        primed_ = true;
    }

    Output process(float requestedSpread, float coordScale) noexcept {
        Output o{};
        bool bad = false;
        float x = sanitize(requestedSpread, &bad);
        o.sanitized = bad;
        const float scale = (std::isfinite(coordScale) && coordScale > 0.f) ? coordScale : 1.0f;
        x *= scale;

        if (x < kMinSpread || x > kMaxSpread) o.clamped = true;
        x = std::clamp(x, kMinSpread, kMaxSpread);

        if (!primed_) { state_ = x; primed_ = true; }
        else          { state_ = state_ + coeff_ * (x - state_); }

        o.spread = state_;
        // Apertura efectiva acotada al radio útil del array: aquí es donde el
        // slider dejaba de tener efecto, no en el slider.
        const float maxOffset = kArrayRadiusM * kMaxApertureFrac;
        o.offsetM = std::clamp(kBaseOffsetM * o.spread, 0.0f, maxOffset);
        return o;
    }

    float spread() const noexcept { return state_; }

private:
    static float sanitize(float v, bool* bad = nullptr) noexcept {
        const bool ok = std::isfinite(v);
        if (bad) *bad = !ok;
        if (!ok) return 1.0f;
        return v;
    }

    float coeff_ = 0.08f;
    float state_ = 1.0f;
    bool  primed_ = false;
};

}  // namespace ivanna::spatial
