// ChebHarmonicShaper.hpp — Excitador Armónico por Polinomios de Chebyshev (T2 Par + T3 Impar)
// con Control Anti-IMD por Planicidad Espectral y Bloqueador DC de 1er Orden.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Especificación Matemática M9 / Sección 4.5 / Prueba T7:
//   - Polinomio de Chebyshev de 2º orden centrado (armónico par 2*f0, calidez de triodo):
//       T2*(u) = 2*u^2 - 1 + (1 - u^2) = u^2
//   - Polinomio de Chebyshev de 3er orden (armónico impar 3*f0, presencia/ataque de cinta):
//       T3*(u) = -1/3 * (4*u^3 - 3*u) = u - (4/3)*u^3
//   - Pesos armónicos gobernados por warmth in [0, 1], drive in [0, 1] y flatness1m (f8):
//       d_eff = drive * (1 - 0.5 * f8)
//       k2 = 0.42 * warmth * d_eff
//       k3 = 0.42 * (1 - 0.85 * warmth) * d_eff
//   - Bloqueador DC de 1er orden a fc = 15 Hz: H_DC(z) = (1 - z^-1) / (1 - R*z^-1)
//     garantizando |DC| < 1e-4 en estado estacionario.
#pragma once

#include <algorithm>
#include <cmath>

namespace ivanna { namespace dsp {

class ChebHarmonicShaper {
public:
    void prepare(float sr) noexcept {
        const float safeSr = (sr > 8000.0f) ? sr : 48000.0f;
        R_ = std::exp(-6.283185307179586f * 15.0f / safeSr);
        reset();
    }

    void reset() noexcept {
        dcX_ = 0.0f;
        dcY_ = 0.0f;
    }

    // x: muestra de entrada
    // drive: [0..1], warmth: [0..1], flatness1m (f8): [0..1]
    float tick(float x, float drive, float warmth, float flatness1m = 0.0f) noexcept {
        if (!std::isfinite(x)) return 0.0f;
        const float d  = std::clamp(drive,      0.0f, 1.0f);
        const float w  = std::clamp(warmth,     0.0f, 1.0f);
        const float f8 = std::clamp(flatness1m, 0.0f, 1.0f);

        // Atenuación Anti-IMD en pasajes armónicamente densos (M9)
        const float dEff = d * (1.0f - 0.5f * f8);

        // Compresor suave de envolvente C^1 para mantener u en (-1, 1) sin generar
        // distorsión impar parásita en niveles nominales (|x| <= 0.85).
        float u = x;
        const float ax = std::fabs(x);
        if (ax > 0.85f) {
            const float ex = ax - 0.85f;
            const float sat = 0.85f + 0.15f * (ex / (0.15f + ex));
            u = (x >= 0.0f) ? sat : -sat;
        }

        const float k2 = 0.42f * w * dEff;
        const float k3 = 0.42f * (1.0f - 0.85f * w) * dEff;

        // T2*(u) = 2*u^2 - 1 + (1 - u^2) = u^2 (2º armónico par puro)
        const float t2 = 2.0f * u * u - 1.0f + (1.0f - u * u);
        // T3(u) = 4*u^3 - 3*u (3er armónico impar puro)
        const float t3 = 4.0f * u * u * u - 3.0f * u;

        const float y = (1.0f - k2 - k3) * u + k2 * t2 + k3 * (t3 * (-0.33333333f));

        // Bloqueador DC de 15 Hz: H(z) = (1 - z^-1) / (1 - R*z^-1)
        const float out = y - dcX_ + R_ * dcY_;
        dcX_ = y;
        dcY_ = std::isfinite(out) ? out : 0.0f;
        return dcY_;
    }

private:
    float R_   = 0.99804f;
    float dcX_ = 0.0f;
    float dcY_ = 0.0f;
};

}} // namespace ivanna::dsp
