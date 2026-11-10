// PsiField.hpp — Operadores del campo Ψ (FORMULACIÓN MAGISTRAL, 2026-11-10).
//
// Implementa en código las ecuaciones de FORMULACION_MAGISTRAL_PSI.md:
//
//   Ec. 2 (CORREGIDA 2026-11-10): la ley de distancia del campo cercano.
//     La primera versión de la formulación escribía
//       ΔILD(r,f) = 20·log10((1+(a/r)(f/f0)) / (1−(a/r)(f/f0)))
//     con f0 = 1500 Hz. Esa forma es FÍSICAMENTE INCORRECTA: al calibrarla
//     contra el modelo geométrico (NearFieldHeadModel::ildDb) el error era
//     de hasta 19.8 dB. La descomposición exacta es:
//       ILD_total(r, θ, f) = SPREAD(r, θ) + SHADOW(f, θ)
//     donde SPREAD = 20·log10(dL/dR) es la divergencia esférica (depende
//     de la distancia, NO de la frecuencia) y SHADOW = 20·log10(1 +
//     0.25·(ka)²·sin²θ) es la sombra cefálica (depende de la frecuencia,
//     NO de la distancia). Verificado en host: la sombra es idéntica
//     (1.29 dB @500 Hz, 4.31 @1 kHz, 21.04 @4 kHz) en r = 0.15…2.0 m,
//     y SPREAD a θ=90° es exactamente 20·log10((r+a)/(r−a)).
//     Esta corrección es la que hace falsable la ley (experimento F1).
//
//   Ec. 3: operador de personalización P_l^m(a) sobre el vector
//     antropométrico CIPIC (17 variables). Pesos v1 de primeros principios
//     (documentados abajo); el ajuste por regresión sobre la base CIPIC
//     real queda PENDIENTE y se marca como tal. Nada aquí pretende ser
//     un ajuste aprendido: son pesos por defecto, fail-closed.
//
//   Ec. 4 (factor Γ): Γ(η) = 1 + γ·(η − η₀), el acoplamiento neural del
//     campo. La EDO completa dη/dt = (η*−η)/τ − λ·L vive en BioControl5D
//     (update() con pérdida opcional); aquí solo está el factor.
//
// Funciones puras, sin estado, sin alloc: seguras en el hilo de audio.
// Header-only, doubles/floats, sin dependencias Android.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace ivanna::psi {

// ── Constantes ──────────────────────────────────────────────────────────────
inline constexpr float kSoundSpeedMps = 343.0f;
inline constexpr float kPi = 3.14159265358979323846f;
// Radio de cabeza por defecto (KEMAR): el mismo que NearFieldHeadModel.
inline constexpr float kHeadRadiusM = 0.0875f;

// ── Ec. 2: ley de distancia + sombra (forma cerrada, verificada) ───────────

// Divergencia esférica (dB) a θ y r: 20·log10(dL/dR), dL/dR = distancias
// directas fuente→oído. A θ=90° se reduce a 20·log10((r+a)/(r−a)).
// Depende de la distancia, NO de la frecuencia. Fail-closed: entradas no
// finitas o r ≤ a devuelven 0.
inline float spreadIldDb(float r, float thetaRad, float headRadiusM = kHeadRadiusM) noexcept {
    if (!std::isfinite(r) || !std::isfinite(thetaRad) || !std::isfinite(headRadiusM)) return 0.0f;
    const float a = std::clamp(headRadiusM, 0.05f, 0.15f);
    const float rr = std::clamp(r, a + 0.01f, 100.0f);
    const float th = std::clamp(thetaRad, -kPi, kPi);
    const float sx = rr * std::sin(th);
    const float sy = rr * std::cos(th);
    const float dR = std::sqrt((sx - a) * (sx - a) + sy * sy);
    const float dL = std::sqrt((sx + a) * (sx + a) + sy * sy);
    if (dR <= 1e-6f || dL <= 1e-6f) return 0.0f;
    return 20.0f * std::log10(dL / dR);
}

// Sombra cefálica (dB): 20·log10(1 + 0.25·(ka)²·sin²θ), con signo según
// el lado (atenúa el oído contralateral). Depende de la frecuencia,
// NO de la distancia. ka = 2πfa/c.
inline float shadowDb(float thetaRad, float freqHz, float headRadiusM = kHeadRadiusM) noexcept {
    if (!std::isfinite(thetaRad) || !std::isfinite(freqHz) || !std::isfinite(headRadiusM)) return 0.0f;
    const float a = std::clamp(headRadiusM, 0.05f, 0.15f);
    const float f = std::clamp(freqHz, 0.0f, 20000.0f);
    const float ka = 2.0f * kPi * f * a / kSoundSpeedMps;
    const float s = std::sin(std::clamp(thetaRad, -kPi, kPi));
    const float mag = 20.0f * std::log10(1.0f + 0.25f * ka * ka * s * s);
    return (s >= 0.0f) ? mag : -mag;
}

// ILD total (dB) de la Ec. 2 corregida: SPREAD + SHADOW.
// Con headRadius = 0.0875 m coincide con NearFieldHeadModel::ildDb
// (misma física, refactorizada con radio personalizable).
inline float ildDb(float r, float thetaRad, float freqHz, float headRadiusM = kHeadRadiusM) noexcept {
    return spreadIldDb(r, thetaRad, headRadiusM) + shadowDb(thetaRad, freqHz, headRadiusM);
}

// ── Ec. 3: vector antropométrico CIPIC + operador de personalización ───────
//
// Variables CIPIC (Algazi et al. 2001), en las unidades de la base:
//   x1  ancho de cabeza [cm]      x2  alto de cabeza [cm]
//   x3  profundidad cabeza [cm]   x4  offset pinna abajo [cm]
//   x5  offset pinna atrás [cm]   x6  ancho de cuello [cm]
//   x7  alto de cuello [cm]       x8  profundidad cuello [cm]
//   x9  ancho torso superior [cm] x10 alto torso superior [cm]
//   x11 ancho torso inferior [cm] x12 alto torso inferior [cm]
//   x13 ancho hombros [cm]        x14 profundidad hombros [cm]
//   x15 ancho cabeza (oreja) [cm] x16 alto pinna [cm]
//   x17 ancho pinna [cm]
// Los valores de referencia son el equivalente KEMAR que ya usa
// HrtfPersonalizer (circunferencia 56 cm → diámetro 17.8 cm; pinna
// 62 mm). NO son medias de la base CIPIC: el ajuste por regresión
// sobre CIPIC queda pendiente y debe reemplazar estos valores.
struct CipicAnthropometry {
    // Valores de referencia (KEMAR-equivalente, ver comentario arriba).
    float x1_headWidthCm = 17.8f;
    float x2_headHeightCm = 24.0f;
    float x3_headDepthCm = 19.0f;
    float x4_pinnaOffsetDownCm = 0.0f;
    float x5_pinnaOffsetBackCm = 0.0f;
    float x6_neckWidthCm = 11.0f;
    float x7_neckHeightCm = 7.0f;
    float x8_neckDepthCm = 11.0f;
    float x9_torsoTopWidthCm = 40.0f;
    float x10_torsoTopHeightCm = 20.0f;
    float x11_torsoBottomWidthCm = 38.0f;
    float x12_torsoBottomHeightCm = 20.0f;
    float x13_shoulderWidthCm = 44.0f;
    float x14_shoulderDepthCm = 22.0f;
    float x15_headWidthEarCm = 17.8f;
    float x16_pinnaHeightMm = 62.0f;
    float x17_pinnaWidthMm = 35.0f;

    float operator[](int i) const noexcept {
        switch (i) {
            case 0: return x1_headWidthCm;
            case 1: return x2_headHeightCm;
            case 2: return x3_headDepthCm;
            case 3: return x4_pinnaOffsetDownCm;
            case 4: return x5_pinnaOffsetBackCm;
            case 5: return x6_neckWidthCm;
            case 6: return x7_neckHeightCm;
            case 7: return x8_neckDepthCm;
            case 8: return x9_torsoTopWidthCm;
            case 9: return x10_torsoTopHeightCm;
            case 10: return x11_torsoBottomWidthCm;
            case 11: return x12_torsoBottomHeightCm;
            case 12: return x13_shoulderWidthCm;
            case 13: return x14_shoulderDepthCm;
            case 14: return x15_headWidthEarCm;
            case 15: return x16_pinnaHeightMm;
            default: return x17_pinnaWidthMm;
        }
    }
};

// Radio de cabeza [m] desde el ancho de cabeza x1 (diámetro/2).
inline float headRadiusFromAnthropometry(const CipicAnthropometry& a) noexcept {
    const float widthM = std::clamp(a.x1_headWidthCm, 10.0f, 25.0f) * 0.01f;
    return std::clamp(widthM * 0.5f, 0.05f, 0.15f);
}

// Pesos v1 de primeros principios para el orden l (suman 1.0 por orden):
//   l=0: monopolo (nivel global) — NO se personaliza (pesos 0).
//   l=1: dipolo (ITD/ILD, dirección) — dominado por el tamaño de cabeza.
//   l=2: cuadrupolo — entran pinna y cuello.
//   l>=3: detalle de pinna y torso.
// Estos pesos son DEFAULTS documentados, no un ajuste aprendido. El
// ajuste por regresión sobre CIPIC debe reemplazarlos (ver TODO).
inline float personalizationWeight(int l, int varIdx) noexcept {
    if (l <= 0) return 0.0f;
    if (varIdx < 0 || varIdx > 16) return 0.0f;
    if (l == 1) {
        switch (varIdx) {
            case 0: return 0.50f;  // ancho cabeza
            case 2: return 0.30f;  // profundidad cabeza
            case 1: return 0.20f;  // alto cabeza
            default: return 0.0f;
        }
    }
    if (l == 2) {
        switch (varIdx) {
            case 3: return 0.30f;  // pinna abajo
            case 4: return 0.30f;  // pinna atrás
            case 0: return 0.20f;  // ancho cabeza
            case 5: return 0.20f;  // ancho cuello
            default: return 0.0f;
        }
    }
    // l >= 3: detalle de pinna (x16, x17) + cabeza + torso.
    switch (varIdx) {
        case 15: return 0.25f;  // alto pinna
        case 16: return 0.25f;  // ancho pinna
        case 0: return 0.15f;   // ancho cabeza
        case 3: return 0.15f;   // pinna abajo
        case 8: return 0.10f;   // torso superior
        case 12: return 0.10f;  // hombros
        default: return 0.0f;
    }
}

// Sensibilidad global por orden: cuánto mueve el factor una desviación
// antropométrica del 100%. Acotada para que el factor nunca se dispare.
inline float personalizationSensitivity(int l) noexcept {
    if (l <= 0) return 0.0f;
    if (l == 1) return 0.50f;
    if (l == 2) return 0.40f;
    return 0.30f;
}

// Ec. 3: P_l^m(a) = 1 + s_l · Σ_j w_{lj}·(x̃_j − 1), con x̃_j = x_j/x_j^ref.
// El factor es 1.0 exacto en la antropometría de referencia y está
// clampado a [0.5, 2.0] (±6 dB): fail-closed ante perfiles extremos.
// m no entra (simetría en m por diseño v1; documentado).
inline float personalizationFactor(int l, int /*m*/, const CipicAnthropometry& a,
                                   const CipicAnthropometry& ref = CipicAnthropometry{}) noexcept {
    if (l <= 0) return 1.0f;
    const float s = personalizationSensitivity(l);
    double acc = 0.0;
    for (int j = 0; j < 17; ++j) {
        const float w = personalizationWeight(l, j);
        if (w == 0.0f) continue;
        const float rj = ref[j];
        if (rj == 0.0f) continue;  // variable de referencia 0 (offset): sin efecto
        const float xj = a[j];
        if (!std::isfinite(xj)) continue;
        acc += static_cast<double>(w) * (static_cast<double>(xj) / rj - 1.0);
    }
    const double f = 1.0 + static_cast<double>(s) * acc;
    return static_cast<float>(std::clamp(f, 0.5, 2.0));
}

// ── Ec. 4 (factor Γ): acoplamiento neural del campo ─────────────────────────
// Γ(η) = 1 + γ·(η − η₀), clampado a [0.5, 1.5] (±3.5 dB). γ default 0.5
// (PROPUESTA_5D.md §3.3a). En η = η₀ devuelve exactamente 1.0.
inline float psiGamma(float eta, float eta0, float gamma = 0.5f) noexcept {
    if (!std::isfinite(eta) || !std::isfinite(eta0) || !std::isfinite(gamma)) return 1.0f;
    const float g = 1.0f + gamma * (eta - eta0);
    return std::clamp(g, 0.5f, 1.5f);
}

}  // namespace ivanna::psi
