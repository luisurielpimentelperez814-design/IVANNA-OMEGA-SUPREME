// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// NEU·Ω — Compilador espectral (Fase 2).
// Convierte los estados por banda de ORBE/HOLOS en matrices complejas
// 2x2 por (banda, frecuencia), que el SpectralCore aplica en un solo
// paso espectral. La matriz se calcula POR BIN porque el ILD, el
// estante de proximidad y la fase ITD dependen de la frecuencia dentro
// de la banda (ver analysis-ivanna/NEU-Omega_Fase2_Matrices.md).
// La composición es A_total = A_HOLOS · A_ORBE (el orden importa).
// PRIVADO: no va al repo público (el diseño NEU·Ω es de Esse).
#pragma once

#include <algorithm>
#include <cmath>

#include "HolosGreenKernel.hpp"
#include "NearFieldHeadModel.hpp"
#include "SpectralCore.hpp"

namespace ivanna::neu {

namespace {
constexpr float kNeuPiF = 3.14159265358979323846f;
}  // namespace

/// Estado por banda de ORBE (lo que estimateBands produce).
/// azimuth/distance/coherence son por banda; el resto es global.
struct OrbeBandState {
    float azimuth[SpectralCore::kMaxBands] = {};    // θ estimado por banda
    float distance[SpectralCore::kMaxBands] = {};   // r estimado por banda
    float coherence[SpectralCore::kMaxBands] = {};  // coherencia por banda
    float diffuse = 0.5f;        // cuánto side pasa al render directo
    float proximityDb = 3.0f;    // techo del realce de proximidad (0..6)
    float headRadiusM = nearfield::kHeadRadiusM;
    float yawRad = 0.0f;         // pose de cabeza (0 si no hay tracking)
};

/// A_ORBE(banda, frecuencia): réplica EXACTA de OrbeBinaural::renderBins
/// expresada como matriz 2x2. Misma matemática, mismos operandos:
///   a = gL·phL, b = gR·phR, c = diffuse (real)
///   A = 0.5 · [[a+c, a−c], [b−c, b+c]]
/// freqHz es la frecuencia CON SIGNO del bin (≥0 en 0..N/2; DC y Nyquist
/// no rotan fase). sr es la tasa de muestreo.
inline NeuMat2x2 orbeMatrix(const OrbeBandState& s, int band, float freqHz,
                            float sampleRate) noexcept {
    NeuMat2x2 A;
    if (band < 0 || band >= SpectralCore::kMaxBands) return A;
    const float theta = std::clamp(s.azimuth[band] * s.coherence[band] - s.yawRad,
                                   -kNeuPiF, kNeuPiF);
    const float r = s.distance[band];
    const float itd = nearfield::itdSeconds(theta, r, s.headRadiusM);
    const float wR = 0.5f * (1.0f + std::sin(theta));
    const float wL = 1.0f - wR;
    const float af = std::fabs(freqHz);
    const float ild = nearfield::ildDb(theta, r, af, s.headRadiusM);
    const float shelf = nearfield::proximityShelfDb(r, af) * (s.proximityDb / 6.0f);
    float gL = std::pow(10.0f, -ild / 40.0f + shelf * wL / 20.0f);
    float gR = std::pow(10.0f, +ild / 40.0f + shelf * wR / 20.0f);
    const float norm = std::sqrt(2.0f / (gL * gL + gR * gR));
    gL *= norm;
    gR *= norm;
    // Fase ITD: DC y Nyquist son reales (sin rotación). En el proveedor
    // por bin freqHz ≥ 0 (k=0..N/2); el Nyquist (k=N/2, f=sr/2) tampoco
    // rota (ORBE lo excluye por k==n/2).
    float phLRe = 1.0f, phLIm = 0.0f, phRRe = 1.0f, phRIm = 0.0f;
    const bool isEdge = (freqHz == 0.0f) ||
                        (std::fabs(freqHz - 0.5f * sampleRate) < 1.0f);
    if (!isEdge) {
        const float w = 2.0f * kNeuPiF * freqHz;
        const float aL = -w * (itd * 0.5f);
        const float aR = -w * (-itd * 0.5f);
        phLRe = std::cos(aL); phLIm = std::sin(aL);
        phRRe = std::cos(aR); phRIm = std::sin(aR);
    }
    // a = gL·phL, b = gR·phR (complejos); c = diffuse (real).
    const float aRe = gL * phLRe, aIm = gL * phLIm;
    const float bRe = gR * phRRe, bIm = gR * phRIm;
    const float c = s.diffuse;
    A.a00r = 0.5f * (aRe + c); A.a00i = 0.5f * aIm;
    A.a01r = 0.5f * (aRe - c); A.a01i = 0.5f * aIm;
    A.a10r = 0.5f * (bRe - c); A.a10i = 0.5f * bIm;
    A.a11r = 0.5f * (bRe + c); A.a11i = 0.5f * bIm;
    return A;
}

/// Estado por banda de HOLOS (lo que HolosOperator estima + Γ por banda).
struct HolosBandState {
    float theta[SpectralCore::kMaxBands] = {};      // azimut por banda
    float distance[SpectralCore::kMaxBands] = {};   // r por banda
    float gammaLin[SpectralCore::kMaxBands] = {};   // Γ_b (lineal)
    float wSide[SpectralCore::kMaxBands] = {};      // widthSide·widthNorm
    float diffuseMix[SpectralCore::kMaxBands] = {}; // mezcla directo/difuso
    float gamma = 1.0f;        // exponente de divergencia (0.5..1.0)
    float proximityDb = 3.0f;  // techo del realce de proximidad (0..6)
    float yawRad = 0.0f;       // pose de cabeza (0 si no hay tracking)
    float pinnaDb = 0.0f;      // pinna diferencial (0 sin tabla cargada)
};

/// A_HOLOS(banda, frecuencia): réplica EXACTA de la parte DIRECTA de
/// HolosOperator::renderBins expresada como matriz 2x2. La cola difusa
/// (T) NO está en la matriz (es Fase 3); aquí solo el directo.
///   a = gL·phL, b = gR·phR, c = wSide·sideDirect (real), s = gammaLin
///   A = s · 0.5 · [[a+c, a−c], [b−c, b+c]]
/// gL/gR/itd vienen de greenGainsBin(greenBandBase(θ,r,γ,proxCap), f, pinna).
/// sideDirect = 1 − diffuseMix. phR = conj(phL) (aR = −aL).
inline NeuMat2x2 holosMatrix(const HolosBandState& s, int band, float freqHz,
                             float sampleRate) noexcept {
    NeuMat2x2 A;
    if (band < 0 || band >= SpectralCore::kMaxBands) return A;
    const float theta = std::clamp(s.theta[band] - s.yawRad, -kNeuPiF, kNeuPiF);
    const float r = s.distance[band];
    const holos::GreenBandBase base =
        holos::greenBandBase(theta, r, s.gamma, s.proximityDb);
    const float af = std::fabs(freqHz);
    const holos::GreenGains g = holos::greenGainsBin(base, af, s.pinnaDb);
    const float itd = g.itdSeconds;
    float phLRe = 1.0f, phLIm = 0.0f, phRRe = 1.0f, phRIm = 0.0f;
    const bool isEdge = (freqHz == 0.0f) ||
                        (std::fabs(freqHz - 0.5f * sampleRate) < 1.0f);
    if (!isEdge) {
        const float w = 2.0f * kNeuPiF * freqHz;
        const float aL = -w * (itd * 0.5f);
        const float c = std::cos(aL), sn = std::sin(aL);
        phLRe = c; phLIm = sn;
        phRRe = c; phRIm = -sn;  // aR = −aL
    }
    const float wSide = s.wSide[band];
    const float sideDirect = 1.0f - s.diffuseMix[band];
    const float c = wSide * sideDirect;  // real
    const float gMid = s.gammaLin[band];
    // a = gL·phL, b = gR·phR (complejos).
    const float aRe = g.gL * phLRe, aIm = g.gL * phLIm;
    const float bRe = g.gR * phRRe, bIm = g.gR * phRIm;
    const float h = 0.5f * gMid;
    A.a00r = h * (aRe + c); A.a00i = h * aIm;
    A.a01r = h * (aRe - c); A.a01i = h * aIm;
    A.a10r = h * (bRe - c); A.a10i = h * bIm;
    A.a11r = h * (bRe + c); A.a11i = h * bIm;
    return A;
}

/// Producto de matrices 2x2 complejas: C = A·B.
/// (A·B)[i][j] = Σ_k A[i][k]·B[k][j] (multiplicación compleja).
inline NeuMat2x2 composeMat(const NeuMat2x2& A, const NeuMat2x2& B) noexcept {
    NeuMat2x2 C;
    // C00 = A00·B00 + A01·B10
    C.a00r = A.a00r * B.a00r - A.a00i * B.a00i + A.a01r * B.a10r - A.a01i * B.a10i;
    C.a00i = A.a00r * B.a00i + A.a00i * B.a00r + A.a01r * B.a10i + A.a01i * B.a10r;
    // C01 = A00·B01 + A01·B11
    C.a01r = A.a00r * B.a01r - A.a00i * B.a01i + A.a01r * B.a11r - A.a01i * B.a11i;
    C.a01i = A.a00r * B.a01i + A.a00i * B.a01r + A.a01r * B.a11i + A.a01i * B.a11r;
    // C10 = A10·B00 + A11·B10
    C.a10r = A.a10r * B.a00r - A.a10i * B.a00i + A.a11r * B.a10r - A.a11i * B.a10i;
    C.a10i = A.a10r * B.a00i + A.a10i * B.a00r + A.a11r * B.a10i + A.a11i * B.a10r;
    // C11 = A10·B01 + A11·B11
    C.a11r = A.a10r * B.a01r - A.a10i * B.a01i + A.a11r * B.a11r - A.a11i * B.a11i;
    C.a11i = A.a10r * B.a01i + A.a10i * B.a01r + A.a11r * B.a11i + A.a11i * B.a11r;
    return C;
}

/// Compilador NEU·Ω (Fase 2): sostiene el estado por banda de ORBE
/// (y HOLOS) y provee A_total por bin al SpectralCore.
/// Uso: crear, llenar el estado (desde OrbeBinaural::bandAzimuth/
/// bandDistance/bandCoherence y HolosOperator::bandState/bandGammaDb/
/// bandWidthGain/bandDiffuseMix tras converger, o con valores fijos
/// en tests), activar useOrbe/useHolos según el operador a compilar,
/// y pasar `NeuCompiler::provider` + `this` a core.setMatrixProvider().
/// Fade (P4, E6): M(w) = I + w·(A_total − I). w=0 → identidad (seco),
/// w=1 → A_total (húmedo). El fundido es álgebra, no conmutación:
/// cero clicks por construcción.
class NeuCompiler {
public:
    OrbeBandState orbe{};
    HolosBandState holos{};
    bool useOrbe = true;
    bool useHolos = false;
    float fade = 1.0f;  // 0..1, peso del fundido (P4)

    static NeuMat2x2 provider(void* ctx, int band, float freqHz,
                              float sampleRate) noexcept {
        auto* self = static_cast<NeuCompiler*>(ctx);
        if (!self) return NeuMat2x2{};
        // A_total = A_HOLOS · A_ORBE (el orden importa: ORBE primero,
        // luego HOLOS). Si solo uno está activo, A_total es ese.
        NeuMat2x2 total;  // identidad
        bool have = false;
        if (self->useOrbe) {
            total = orbeMatrix(self->orbe, band, freqHz, sampleRate);
            have = true;
        }
        if (self->useHolos) {
            const NeuMat2x2 h = holosMatrix(self->holos, band, freqHz, sampleRate);
            total = have ? composeMat(h, total) : h;
            have = true;
        }
        if (!have) return total;  // identidad
        // Fade: M(w) = I + w·(A_total − I).
        const float w = std::clamp(self->fade, 0.0f, 1.0f);
        if (w >= 1.0f) return total;
        if (w <= 0.0f) return NeuMat2x2{};
        NeuMat2x2 M;
        M.a00r = 1.0f + w * (total.a00r - 1.0f); M.a00i = w * total.a00i;
        M.a01r = w * total.a01r;                 M.a01i = w * total.a01i;
        M.a10r = w * total.a10r;                 M.a10i = w * total.a10i;
        M.a11r = 1.0f + w * (total.a11r - 1.0f); M.a11i = w * total.a11i;
        return M;
    }
};

}  // namespace ivanna::neu
