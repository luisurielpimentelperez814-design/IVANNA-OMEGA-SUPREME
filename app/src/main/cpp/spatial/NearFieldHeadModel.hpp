// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// NearFieldHeadModel — modelo de cabeza esférica en campo cercano (ORBE).
//
// Física: cabeza rígida esférica de radio a = 8.75 cm (el mismo que ya usa
// HeadphoneCrossfeed.hpp), velocidad del sonido c = 343 m/s. Para una fuente
// a azimut θ (0 = frontal, + = derecha) y distancia r, el ITD y el ILD se
// calculan con el modelo geométrico de rayos sobre la esfera (Kuhn /
// Duda & Martens): el camino al oído ipsilateral es directo; al oído
// contralateral el sonido bordea la esfera (tangente + arco). En campo
// lejano (r → ∞) el ITD se reduce exactamente a Woodworth:
//   ITD = (a/c)·(θ + sinθ)          (|θ| ≤ 90°)
//   ITD = (a/c)·(π − θ + sinθ)      (|θ| > 90°)
// y el ILD en graves → 0. Al acercarse la fuente, el ITD crece (~+12 % a
// 0.15 m / 90°) y el ILD en graves —casi nulo en campo lejano— crece hasta
// ~15–20 dB por divergencia esférica (presión ∝ 1/d) más sombra cefálica.
// Esto es física de difracción medida desde los años 90, no una invención.
//
// Funciones puras, sin estado, sin alloc: seguras en el hilo de audio y
// testeables sin audio (ver tests/test_near_field_head_model.cpp).
// Convenciones: itdSeconds > 0 significa que el oído IZQUIERDO llega más
// tarde (fuente a la derecha, θ > 0). ildDb > 0 significa que el oído
// DERECHO es más intenso (fuera a la derecha). Ambas son antisimétricas
// en θ. proximityShelfDb es el realce de graves del oído ipsilateral
// respecto a una referencia de 1 m, acotado a +6 dB.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>

namespace ivanna {

namespace nearfield {

inline constexpr float kHeadRadiusM = 0.0875f;  // a = 8.75 cm
inline constexpr float kSoundSpeedMps = 343.0f; // c
inline constexpr float kPi = 3.14159265358979323846f;

// Longitud del camino acústico (rayos) desde una fuente a un oído.
// earSign: +1 oído derecho (+a, 0), −1 oído izquierdo (−a, 0).
// Fuente en (r·sinθ, r·cosθ): θ = 0 frontal, +θ hacia la derecha.
// El camino es el mínimo entre el rayo directo (si no atraviesa la
// esfera) y los dos caminos de borde (tangente + arco). El primero en
// llegar es el camino más corto (Fermat).
// headRadiusM: radio de cabeza personalizado (PsiFieldRuntime / ORBE);
// por defecto KEMAR 8.75 cm — sin él, bit-idéntico al modelo original.
inline float pathLengthToEar(float thetaRad, float r, float earSign,
                             float headRadiusM = kHeadRadiusM) noexcept {
    if (!std::isfinite(thetaRad) || !std::isfinite(r)) return 1.0f;
    const float a = std::clamp(std::isfinite(headRadiusM) ? headRadiusM : kHeadRadiusM,
                               0.05f, 0.15f);
    const float rr = std::clamp(r, a + 0.01f, 100.0f);
    const float th = std::clamp(thetaRad, -kPi, kPi);
    const float sx = rr * std::sin(th);
    const float sy = rr * std::cos(th);
    const float ex = earSign * a;
    const float ey = 0.0f;
    // Rayo directo.
    const float dx = sx - ex, dy = sy - ey;
    const float direct = std::sqrt(dx * dx + dy * dy);
    // ¿El segmento S→E atraviesa la esfera? Distancia del origen al segmento.
    // El punto del segmento es S − t·(S−E): t = (S·(S−E))/|S−E|².
    // Solo cuenta si el punto más cercano es INTERIOR (0 < t < 1): en los
    // extremos el punto es la fuente (fuera) o el oído (sobre la esfera,
    // distancia exactamente a) y el rayo directo no está bloqueado. Sin
    // esta distinción, el ruido float en el extremo hacía que el oído
    // ipsilateral pareciera bloqueado y el ITD colapsara a ~0.
    const float segLen2 = dx * dx + dy * dy;
    float blocked = false;
    if (segLen2 > 1e-12f) {
        const float t = (sx * dx + sy * dy) / segLen2;  // proyección del origen
        if (t > 1e-6f && t < 1.0f - 1e-6f) {
            const float cx = sx - t * dx, cy = sy - t * dy;
            blocked = (cx * cx + cy * cy) < (a * a * (1.0f - 1e-6f));
        }
    }
    float best = blocked ? 1.0e30f : direct;
    // Caminos de borde: tangente desde S + arco hasta el oído.
    const float tangLen = std::sqrt(rr * rr - a * a);
    const float gamma = std::atan2(sy, sx);              // ángulo de la fuente
    const float beta = std::acos(a / rr);                 // ángulo OS–OT
    const float earAng = std::atan2(ey, ex);              // 0 (der) o π (izq)
    for (int s = -1; s <= 1; s += 2) {
        const float tangAng = gamma + (float)s * beta;
        float dAng = tangAng - earAng;
        while (dAng > kPi) dAng -= 2.0f * kPi;
        while (dAng < -kPi) dAng += 2.0f * kPi;
        const float total = tangLen + a * std::fabs(dAng);
        if (total < best) best = total;
    }
    return best;
}

// ITD esférico (segundos): retardo del oído izquierdo menos el derecho.
// θ > 0 (fuente a la derecha) → ITD > 0. En r → ∞ coincide con Woodworth.
inline float itdSeconds(float thetaRad, float r,
                        float headRadiusM = kHeadRadiusM) noexcept {
    if (!std::isfinite(thetaRad) || !std::isfinite(r)) return 0.0f;
    const float pathL = pathLengthToEar(thetaRad, r, -1.0f, headRadiusM);
    const float pathR = pathLengthToEar(thetaRad, r, +1.0f, headRadiusM);
    return (pathL - pathR) / kSoundSpeedMps;
}

// Woodworth de campo lejano, para referencia y tests (r → ∞).
inline float itdWoodworthFarField(float thetaRad) noexcept {
    if (!std::isfinite(thetaRad)) return 0.0f;
    const float a = kHeadRadiusM, c = kSoundSpeedMps;
    const float th = std::fabs(std::clamp(thetaRad, -kPi, kPi));
    if (th <= kPi * 0.5f) return (a / c) * (th + std::sin(th));
    return (a / c) * (kPi - th + std::sin(th));
}

// ILD esférico (dB, derecho menos izquierdo): > 0 si la fuente está a la
// derecha. Suma divergencia esférica (1/d, distancia directa fuente→oído)
// y sombra cefálica dependiente de la frecuencia (ka = 2πfa/c).
inline float ildDb(float thetaRad, float r, float freqHz,
                   float headRadiusM = kHeadRadiusM) noexcept {
    if (!std::isfinite(thetaRad) || !std::isfinite(r) || !std::isfinite(freqHz)) return 0.0f;
    const float a = std::clamp(std::isfinite(headRadiusM) ? headRadiusM : kHeadRadiusM,
                               0.05f, 0.15f);
    const float rr = std::clamp(r, a + 0.01f, 100.0f);
    const float th = std::clamp(thetaRad, -kPi, kPi);
    const float sx = rr * std::sin(th);
    const float sy = rr * std::cos(th);
    const float dR = std::sqrt((sx - a) * (sx - a) + sy * sy);  // fuente→oído der
    const float dL = std::sqrt((sx + a) * (sx + a) + sy * sy);  // fuente→oído izq
    const float spreadingDb = 20.0f * std::log10(dL / dR);
    const float f = std::clamp(freqHz, 0.0f, 20000.0f);
    const float ka = 2.0f * kPi * f * a / kSoundSpeedMps;
    // La sombra atenúa al oído CONTRALATERAL: suma al ILD si la fuente está
    // a la derecha (atenúa L) y resta si está a la izquierda (atenúa R).
    const float shadowMagDb = 20.0f * std::log10(1.0f + 0.25f * ka * ka * std::sin(th) * std::sin(th));
    const float shadowDb = (std::sin(th) >= 0.0f) ? shadowMagDb : -shadowMagDb;
    return spreadingDb + shadowDb;
}

// Realce de proximidad esférico (dB) del oído ipsilateral respecto a una
// referencia de 1 m: solo graves (shelf con esquina ~300 Hz), acotado a
// +6 dB. A r ≥ 1 m es 0. Es el mismo fenómeno que el efecto de proximidad
// de un micrófono direccional, entre la fuente virtual y cada oído.
inline float proximityShelfDb(float r, float freqHz) noexcept {
    if (!std::isfinite(r) || !std::isfinite(freqHz)) return 0.0f;
    if (r >= 1.0f) return 0.0f;
    const float rr = std::clamp(r, 0.10f, 1.0f);
    const float raw = 20.0f * std::log10(1.0f / rr);
    const float capped = std::min(raw, 6.0f);
    const float f = std::clamp(freqHz, 0.0f, 20000.0f);
    const float shape = 1.0f / (1.0f + (f / 300.0f) * (f / 300.0f));
    return capped * shape;
}

}  // namespace nearfield

}  // namespace ivanna
