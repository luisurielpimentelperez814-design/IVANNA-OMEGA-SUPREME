// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// HolosGreenKernel — G_b: la función de Green unificada de HOLOS.
//
// FUSION.md §4.4.1: HOA, WFS, RIR, HRTF, ORBE y crossfeed son todos
// soluciones de la misma ecuación de onda con condiciones de contorno
// distintas. G_b las evalúa UNA vez por banda ERB en vez de cinco veces
// con cinco constantes distintas:
//
//   Directo: fase = exp(∓jω·ITD_esférico(θ,r)/2)  [rotación de fase exacta]
//            magnitud = ILD_esférico(θ,r,f) · pinna(θ,f) · (1/r)^γ
//   Difuso:  (1 − coh_b) → cola de sala compartida (RirConvolver)
//
// Funciones puras, sin estado, sin alloc: testeables sin audio
// (tests/test_holos_green_kernel.cpp). Reutilizan NearFieldHeadModel
// (el modelo esférico ya testeado de ORBE) — no lo reescriben.
//
// Decisiones de v1 (honestas, documentadas):
// - γ (exponente de divergencia) entra SOLO diferencialmente: el término
//   de divergencia 20·log10(dL/dR) del ILD se escala por γ. γ=1 → ley
//   esférica 1/r (ORBE/HRTF); γ=½ → ley cilíndrica 1/√d (WFS 2.5D).
//   El nivel GLOBAL (1/r)^γ se cancela con la normalización de potencia
//   constante, como ORBE: HOLOS nunca sube el nivel global (FUSION §5.4).
// - pinna(θ,f): magnitud de pinna MEDIDA, interpolada linealmente (v1;
//   OT en fase 2). Sin tabla cargada → 0 dB (identidad). No se inventa
//   ninguna curva: la tabla se alimenta desde HrtfManager/datasets .ihr1
//   cuando exista; hasta entonces la coloración de pabellón sigue llegando
//   por el camino legacy HOA+HRTF. En v1 la pinna es un término
//   DIFERENCIAL (R−L, como el ILD); la tabla por oído separada es fase 2.
// - El crossfeed es el límite de baja frecuencia de esta misma fórmula
//   (f<700 Hz, θ=30° fijo): no es un módulo aparte.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>
#include <vector>

#include "NearFieldHeadModel.hpp"

namespace ivanna {
namespace holos {

inline constexpr float kPiF = 3.14159265358979323846f;

// ILD esférico con exponente de divergencia γ (FUSION §4.4.1).
// El término de divergencia 20·log10(dL/dR) se escala por γ; la sombra
// cefálica dependiente de la frecuencia no depende de γ.
// Se deriva de nearfield::ildDb sin duplicar su geometría: a f=0 la sombra
// es cero, así que ildDb(θ,r,0) ES el término de divergencia puro.
inline float ildDbGamma(float thetaRad, float r, float freqHz, float gamma) noexcept {
    if (!std::isfinite(thetaRad) || !std::isfinite(r) || !std::isfinite(freqHz) ||
        !std::isfinite(gamma))
        return 0.0f;
    const float g = std::clamp(gamma, 0.5f, 1.0f);
    const float spreading = nearfield::ildDb(thetaRad, r, 0.0f);
    const float total = nearfield::ildDb(thetaRad, r, freqHz);
    const float shadow = total - spreading;
    return g * spreading + shadow;
}

// Ganancias binaurales de G_b para una banda (θ, r, f):
// ILD (con γ) + magnitud de pinna diferencial + estante de proximidad,
// con normalización de potencia constante (la parte común se cancela).
struct GreenGains {
    float gL = 1.0f;         // ganancia oído izquierdo (lineal)
    float gR = 1.0f;         // ganancia oído derecho (lineal)
    float itdSeconds = 0.0f; // ITD esférico (s); >0 = fuente a la derecha
    float ildDb = 0.0f;      // ILD aplicado (dB, R−L)
    float proximityDb = 0.0f; // estante de proximidad aplicado (dB)
};

inline GreenGains greenGains(float thetaRad, float r, float freqHz, float gamma,
                             float pinnaDb, float proximityCapDb) noexcept {
    GreenGains out;
    if (!std::isfinite(thetaRad) || !std::isfinite(r) || !std::isfinite(freqHz))
        return out;
    const float theta = std::clamp(thetaRad, -kPiF, kPiF);
    const float rr = std::clamp(r, 0.15f, 2.0f);
    const float f = std::clamp(freqHz, 0.0f, 20000.0f);
    out.itdSeconds = nearfield::itdSeconds(theta, rr);
    out.ildDb = ildDbGamma(theta, rr, f, gamma);
    const float proxScale = std::clamp(proximityCapDb, 0.0f, 6.0f) / 6.0f;
    out.proximityDb = nearfield::proximityShelfDb(rr, f) * proxScale;
    // La pinna diferencial se suma al ILD (misma convención R−L).
    const float ildTotal = out.ildDb + std::clamp(pinnaDb, -24.0f, 24.0f);
    float gL = std::pow(10.0f, -ildTotal / 40.0f);
    float gR = std::pow(10.0f, +ildTotal / 40.0f);
    // Estante de proximidad repartido por lateralización (wR = ½(1+sinθ)),
    // como ORBE: en el centro ambos oídos reciben la mitad y la parte común
    // la cancela la normalización — el estante es un componente de ILD,
    // no una ganancia global.
    const float wR = 0.5f * (1.0f + std::sin(theta));
    const float wL = 1.0f - wR;
    gL *= std::pow(10.0f, out.proximityDb * wL / 20.0f);
    gR *= std::pow(10.0f, out.proximityDb * wR / 20.0f);
    const float norm = std::sqrt(2.0f / (gL * gL + gR * gR));
    out.gL = gL * norm;
    out.gR = gR * norm;
    return out;
}

// Mezcla directo/difuso de una banda: cuánto del side va a la cola
// compartida (0 = todo directo, 1 = todo difuso). La coherencia gobierna:
// fuente puntual (coh≈1) → directo; campo difuso (coh≈0) → cola.
inline float diffuseMix(float coherence, float diffuseParam) noexcept {
    if (!std::isfinite(coherence) || !std::isfinite(diffuseParam)) return 0.0f;
    const float coh = std::clamp(coherence, 0.0f, 1.0f);
    const float d = std::clamp(diffuseParam, 0.0f, 1.0f);
    return (1.0f - coh) * d;
}

// ── Reforma v2.5.429: G_b por banda + por bin ──────────────────────────
// Problema medido en vivo (2026-11-12 23:43, v2.5.428): HOLOS p99 13–25 ms
// (presupuesto 6.67 ms). El cuello es renderBins(): llamaba a greenGains()
// por BIN (512×/frame), y greenGains() recalcula por bin cosas que solo
// dependen de la BANDA (24 bandas): itdSeconds() (2× pathLengthToEar con
// atan2/acos/sin/cos/sqrt), el ILD por divergencia ildDb(θ,r,0) y
// sin(θ). La reforma separa G_b en dos partes con la MISMA matemática
// (bit-idéntica a greenGains, verificada por test):
//   - GreenBandBase: todo lo independiente de la frecuencia, 1×/banda/frame.
//   - greenGainsBin(): solo lo dependiente de f (sombra cefálica,
//     estante de proximidad, pow de ganancias), 1×/bin.
// El render pasa de ~15–20 trascendentales por bin a ~4 (1 log10, 4 pow,
// 1 sqrt). HOLOS deja de ser el cuello y corre a 24 bandas dentro del
// presupuesto — IVANNA cumple lo que ofrece, sin bypass ni degradación.
// greenGains() se conserva (tests y referencia); el camino vivo usa la
// versión por banda+bin.
struct GreenBandBase {
    float theta = 0.0f;        // azimut de banda (clamp −π..π)
    float itdSeconds = 0.0f;   // ITD esférico (s), independiente de f
    float spreadingDb = 0.0f;  // ILD por divergencia 20·log10(dL/dR) (f=0)
    float sinTheta = 0.0f;
    float sin2Theta = 0.0f;    // sin²θ (sombra cefálica)
    float wR = 0.5f, wL = 0.5f;  // reparto de proximidad por lateralización
    float proxCapDb = 0.0f;    // min(20·log10(1/r), 6), 0 si r≥1 m
    float gamma = 1.0f;        // exponente de divergencia (clamp 0.5..1)
    float proxScale = 0.0f;    // clamp(proximityCapDb,0,6)/6
};

inline GreenBandBase greenBandBase(float thetaRad, float r, float gamma,
                                   float proximityCapDb) noexcept {
    GreenBandBase b;
    if (!std::isfinite(thetaRad) || !std::isfinite(r) || !std::isfinite(gamma) ||
        !std::isfinite(proximityCapDb))
        return b;
    const float theta = std::clamp(thetaRad, -kPiF, kPiF);
    const float rr = std::clamp(r, 0.15f, 2.0f);
    b.theta = theta;
    b.itdSeconds = nearfield::itdSeconds(theta, rr);
    // A f=0 la sombra es 0 (ka=0), así que ildDb(θ,r,0) ES el spreading.
    b.spreadingDb = nearfield::ildDb(theta, rr, 0.0f);
    b.sinTheta = std::sin(theta);
    b.sin2Theta = b.sinTheta * b.sinTheta;
    b.wR = 0.5f * (1.0f + b.sinTheta);
    b.wL = 1.0f - b.wR;
    if (rr >= 1.0f) {
        b.proxCapDb = 0.0f;
    } else {
        b.proxCapDb = std::min(20.0f * std::log10(1.0f / rr), 6.0f);
    }
    b.gamma = std::clamp(gamma, 0.5f, 1.0f);
    b.proxScale = std::clamp(proximityCapDb, 0.0f, 6.0f) / 6.0f;
    return b;
}

// G_b por bin desde la base de banda: misma matemática que greenGains().
// Solo lo dependiente de f: sombra cefálica (ka=2πfa/c) y estante de
// proximidad (shape 1/(1+(f/300)²)); las ganancias combinan los mismos
// 4 pow() que greenGains() en el mismo orden.
inline GreenGains greenGainsBin(const GreenBandBase& b, float freqHz,
                                float pinnaDb) noexcept {
    GreenGains out;
    if (!std::isfinite(freqHz) || !std::isfinite(pinnaDb)) return out;
    const float f = std::clamp(freqHz, 0.0f, 20000.0f);
    const float ka = 2.0f * kPiF * f * nearfield::kHeadRadiusM /
                     nearfield::kSoundSpeedMps;
    const float shadowMagDb =
        20.0f * std::log10(1.0f + 0.25f * ka * ka * b.sin2Theta);
    const float shadowDb = (b.sinTheta >= 0.0f) ? shadowMagDb : -shadowMagDb;
    const float ildDb = b.gamma * b.spreadingDb + shadowDb;
    const float shape = 1.0f / (1.0f + (f / 300.0f) * (f / 300.0f));
    const float proximityDb = b.proxCapDb * shape * b.proxScale;
    const float ildTotal = ildDb + std::clamp(pinnaDb, -24.0f, 24.0f);
    float gL = std::pow(10.0f, -ildTotal / 40.0f);
    float gR = std::pow(10.0f, +ildTotal / 40.0f);
    gL *= std::pow(10.0f, proximityDb * b.wL / 20.0f);
    gR *= std::pow(10.0f, proximityDb * b.wR / 20.0f);
    const float norm = std::sqrt(2.0f / (gL * gL + gR * gR));
    out.gL = gL * norm;
    out.gR = gR * norm;
    out.itdSeconds = b.itdSeconds;
    out.ildDb = ildDb;
    out.proximityDb = proximityDb;
    return out;
}

// Tabla de magnitud de pinna medida (diferencial R−L, dB), interpolación
// lineal en azimut y en log-frecuencia (v1; transporte óptimo en fase 2).
// Sin tabla cargada → 0 dB (identidad). La tabla se alimenta desde
// HrtfManager/datasets .ihr1; NUNCA se rellena con una curva inventada.
struct PinnaTable {
    std::vector<float> azimuthsRad; // crecientes, p. ej. −π/2..π/2
    std::vector<float> freqsHz;     // crecientes, p. ej. 1k..16k
    std::vector<float> magDb;       // [azimut][frecuencia], diferencial R−L

    bool empty() const noexcept { return azimuthsRad.empty() || freqsHz.empty(); }

    void set(const std::vector<float>& az, const std::vector<float>& fr,
             const std::vector<float>& mag) noexcept {
        if (az.empty() || fr.empty() || mag.size() != az.size() * fr.size()) return;
        azimuthsRad = az;
        freqsHz = fr;
        magDb = mag;
    }

    float pinnaDb(float thetaRad, float freqHz) const noexcept {
        if (empty() || !std::isfinite(thetaRad) || !std::isfinite(freqHz)) return 0.0f;
        const float th = std::clamp(thetaRad, azimuthsRad.front(), azimuthsRad.back());
        const float f = std::clamp(freqHz, freqsHz.front(), freqsHz.back());
        // Índices de interpolación (lineal en θ, lineal en log-f).
        size_t ia = 0;
        while (ia + 1 < azimuthsRad.size() && azimuthsRad[ia + 1] < th) ++ia;
        size_t ib = 0;
        const float lf = std::log(std::max(f, 1.0f));
        while (ib + 1 < freqsHz.size() &&
               std::log(std::max(freqsHz[ib + 1], 1.0f)) < lf)
            ++ib;
        const float tA = (azimuthsRad.size() > 1 && ia + 1 < azimuthsRad.size())
                             ? (th - azimuthsRad[ia]) /
                                   std::max(azimuthsRad[ia + 1] - azimuthsRad[ia], 1e-9f)
                             : 0.0f;
        const float l0 = std::log(std::max(freqsHz[ib], 1.0f));
        const float l1 = (ib + 1 < freqsHz.size())
                             ? std::log(std::max(freqsHz[ib + 1], 1.0f))
                             : l0;
        const float tB = (l1 > l0) ? (lf - l0) / (l1 - l0) : 0.0f;
        const size_t na = azimuthsRad.size(), nf = freqsHz.size();
        const size_t ja = std::min(ia + 1, na - 1), jb = std::min(ib + 1, nf - 1);
        const float v00 = magDb[ia * nf + ib];
        const float v10 = magDb[ja * nf + ib];
        const float v01 = magDb[ia * nf + jb];
        const float v11 = magDb[ja * nf + jb];
        return (v00 * (1.0f - tA) + v10 * tA) * (1.0f - tB) +
               (v01 * (1.0f - tA) + v11 * tA) * tB;
    }
};

}  // namespace holos
}  // namespace ivanna
