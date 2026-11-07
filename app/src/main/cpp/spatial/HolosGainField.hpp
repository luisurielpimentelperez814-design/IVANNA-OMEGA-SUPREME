// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// HolosGainField — Γ_b: el campo de ganancia único de HOLOS.
//
// FUSION.md §4.4.3: compresor, limiter, dos EQs, tres M/S, trims y tilts
// son todos multiplicaciones puntuales por banda e instante. HOLOS calcula
// UNA sola ganancia por banda:
//
//   Γ_b = Γ_estático(b) · Γ_dinámico(b,t) · Γ_ancho(b) · Γ_techo(b,t)
//
// Funciones puras, sin estado, sin alloc: testeables sin audio
// (tests/test_holos_gain_field.cpp). Cada ley es la del módulo legacy que
// absorbe, con sus constantes exactas leídas del código:
//
// - Γ_estático: la magnitud del ParametricEQ (8 biquads RBJ en cascada,
//   dsp/ParametricEQ.cpp::setBandShaped) evaluada analíticamente en el
//   centro de cada banda ERB. La banda muerta (500 Hz, ganancia 0 siempre)
//   y el m_evolutionPath muerto desaparecen. El FIR de 256 taps del EvoEQ
//   (fase lineal, latencia 127.5 muestras) se reemplaza por magnitud por
//   bin (fase cero): la curva es la misma, la fase no (FUSION §5.5).
// - Γ_dinámico: la ley del compresor DSP (dsp/Compressor.cpp): soft-knee
//   cuadrático ±3 dB, makeup acoplado sin ganancia gratuita. UN solo
//   default canónico: threshold −15 dB / ratio 3:1 (el de DSPParams;
//   FUSION §4.4.3 resuelve así el conflicto de tres defaults).
// - Γ_ancho: el operador M/S único: ganancia de side desde θ/coh, con el
//   split de graves del StereoWidener (bassFactor) y la norma de potencia
//   constante del M/S de FusionCore (1/√(0.5(1+w²))).
// - Γ_techo: la racional softCeil del SafetyLimiter (dsp/SafetyLimiter.cpp)
//   + la ganancia de pico del limiter. Es el único término que NO se
//   absorbe del todo: la guarda final sigue siendo un módulo independiente
//   (FUSION §5.4). En el operador, Γ_techo se evalúa en el dominio temporal
//   sobre el hop renderizado (un pico verdadero solo existe en tiempo;
//   recortar magnitud por bin distorsionaría la fase).
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>

namespace ivanna {
namespace holos {

// ── Γ_estático: magnitud RBJ del ParametricEQ ──────────────────────────────
// Parámetros de EQ en dB, mismo mapeo que ParametricEQ::setParams
// (dsp/ParametricEQ.cpp): low/mid/high/presence + fatigueTilt + freq/Q.
struct EqParams {
    float low = 0.0f;       // dB, low-shelf 90 Hz
    float mid = 0.0f;       // dB, picos 2500 Hz (×1.0) y freq (×0.6)
    float high = 0.0f;      // dB, pico 5000 Hz (×0.35) + high-shelf 10 kHz
    float presence = 0.0f;  // dB, pico 8000 Hz
    float tiltDb = 0.0f;    // dB, fatigueTilt [−3, 0]
    float freq = 1000.0f;   // Hz, banda paramétrica libre
    float resonance = 0.707f; // Q de las campanas
};

namespace detail {
// Coeficientes RBJ (doble precisión), copia exacta de
// ParametricEQ::setBandShaped. shape: 0=Peak, 1=LowShelf, 2=HighShelf.
inline void rbjCoeffs(int shape, double f, double q, double gDb, double sr,
                      double& b0, double& b1, double& b2, double& a1, double& a2) {
    const double A = std::pow(10.0, gDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = std::cos(w0), sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    double db0, db1, db2, da0, da1, da2;
    if (shape == 1) {  // LowShelf
        const double tsa = 2.0 * std::sqrt(A) * alpha;
        db0 = A * ((A + 1.0) - (A - 1.0) * c + tsa);
        db1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c);
        db2 = A * ((A + 1.0) - (A - 1.0) * c - tsa);
        da0 = (A + 1.0) + (A - 1.0) * c + tsa;
        da1 = -2.0 * ((A - 1.0) + (A + 1.0) * c);
        da2 = (A + 1.0) + (A - 1.0) * c - tsa;
    } else if (shape == 2) {  // HighShelf
        const double tsa = 2.0 * std::sqrt(A) * alpha;
        db0 = A * ((A + 1.0) + (A - 1.0) * c + tsa);
        db1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
        db2 = A * ((A + 1.0) + (A - 1.0) * c - tsa);
        da0 = (A + 1.0) - (A - 1.0) * c + tsa;
        da1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
        da2 = (A + 1.0) - (A - 1.0) * c - tsa;
    } else {  // Peak
        db0 = 1.0 + alpha * A; db1 = -2.0 * c; db2 = 1.0 - alpha * A;
        da0 = 1.0 + alpha / A; da1 = -2.0 * c; da2 = 1.0 - alpha / A;
    }
    b0 = db0 / da0; b1 = db1 / da0; b2 = db2 / da0;
    a1 = da1 / da0; a2 = da2 / da0;
}

// |H(e^jw)| de un biquad en frecuencia f.
inline double biquadMagnitude(double b0, double b1, double b2, double a1, double a2,
                              double f, double sr) {
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double cw = std::cos(w), sw = std::sin(w);
    const double cw2 = std::cos(2.0 * w), sw2 = std::sin(2.0 * w);
    const double br = b0 + b1 * cw + b2 * cw2;
    const double bi = -(b1 * sw + b2 * sw2);
    const double ar = 1.0 + a1 * cw + a2 * cw2;
    const double ai = -(a1 * sw + a2 * sw2);
    return std::sqrt((br * br + bi * bi) / std::max(ar * ar + ai * ai, 1e-30));
}
}  // namespace detail

// Ganancia estática (lineal) del EQ en la frecuencia f: producto de las
// magnitudes de las 8 bandas del ParametricEQ. Con todos los parámetros
// en 0 → 1.0 exacto (no-op bit-exacto, como el paramétrico plano).
inline float staticGainLin(float freqHz, const EqParams& eq, float sampleRate) noexcept {
    if (!std::isfinite(freqHz) || !std::isfinite(sampleRate) || sampleRate < 8000.0f)
        return 1.0f;
    const double sr = sampleRate;
    const double f = std::clamp((double)freqHz, 20.0, sr * 0.5 - 100.0);
    const double q = std::clamp((double)eq.resonance, 0.1, 10.0);
    const double tilt = eq.tiltDb;
    auto clampDb = [](double db) { return std::clamp(db, -18.0, 18.0); };
    // (shape, f, q, g) — mismo mapeo que ParametricEQ::setParams.
    struct Band { int shape; double f; double q; double g; };
    const Band bands[8] = {
        {1, 90.0, 0.707, clampDb(eq.low)},
        {0, 250.0, 1.0, (eq.low > 0.0f) ? clampDb(-0.08 * (double)eq.low) : 0.0},
        {0, 500.0, q, 0.0},  // banda muerta (ganancia 0 siempre)
        {0, std::clamp((double)eq.freq, 20.0, sr * 0.5 - 100.0), q, clampDb(eq.mid * 0.6)},
        {0, 2500.0, q, clampDb(eq.mid)},
        {0, 5000.0, q, clampDb(eq.high * 0.35 + tilt / 3.0)},
        {0, 8000.0, q, clampDb(eq.presence + tilt * (2.0 / 3.0))},
        {2, 10000.0, 0.707, clampDb(eq.high + tilt)},
    };
    double mag = 1.0;
    for (const Band& b : bands) {
        double b0, b1, b2, a1, a2;
        detail::rbjCoeffs(b.shape, b.f, b.q, b.g, sr, b0, b1, b2, a1, a2);
        mag *= detail::biquadMagnitude(b0, b1, b2, a1, a2, f, sr);
    }
    return (float)mag;
}

// ── Γ_dinámico: ley de compresión del Compressor DSP ───────────────────────
// Default canónico único (FUSION §4.4.3): threshold −15 dB / ratio 3:1.
// Devuelve la ganancia lineal total (reducción + makeup acoplado).
// envDb: nivel de la envolvente unificada en dBFS (20·log10(env)).
inline float dynamicGainLin(float envDb, float thresholdDb = -15.0f, float ratio = 3.0f,
                            float runtimeAmount = 0.0f) noexcept {
    if (!std::isfinite(envDb)) return 1.0f;
    const float thr = thresholdDb - runtimeAmount * 12.0f;
    const float effRatio = std::max(ratio + runtimeAmount * 8.0f, 1.0f);
    const float ratioInv = 1.0f - 1.0f / effRatio;
    const float overDb = envDb - thr;
    float gainDb = 0.0f;
    constexpr float kKneeHalf = 3.0f;
    if (overDb <= -kKneeHalf) {
        gainDb = 0.0f;
    } else if (overDb < kKneeHalf) {
        const float t = (overDb + kKneeHalf) / (2.0f * kKneeHalf);
        gainDb = -(ratioInv * kKneeHalf) * t * t;
    } else {
        gainDb = overDb * (-ratioInv);
    }
    // Makeup acoplado a la reducción REAL (nunca hay ganancia gratuita
    // bajo threshold): techo = −threshold·(1−1/R)·0.5 + aporte runtime.
    const float makeupCeilDb =
        std::max(0.0f, -thresholdDb * (1.0f - 1.0f / ratio) * 0.5f) +
        runtimeAmount * 12.0f * (1.0f - 1.0f / effRatio) * 0.10f;
    const float makeupDb = std::min(-gainDb, makeupCeilDb);
    return std::exp((gainDb + makeupDb) * 0.11512925465f);
}

// Ganancia de compresión en dB (sin makeup), para tests de la curva.
inline float dynamicGainDbNoMakeup(float envDb, float thresholdDb = -15.0f,
                                   float ratio = 3.0f) noexcept {
    if (!std::isfinite(envDb)) return 0.0f;
    const float ratioInv = 1.0f - 1.0f / std::max(ratio, 1.0f);
    const float overDb = envDb - thresholdDb;
    constexpr float kKneeHalf = 3.0f;
    if (overDb <= -kKneeHalf) return 0.0f;
    if (overDb < kKneeHalf) {
        const float t = (overDb + kKneeHalf) / (2.0f * kKneeHalf);
        return -(ratioInv * kKneeHalf) * t * t;
    }
    return overDb * (-ratioInv);
}

// ── Γ_ancho: el operador M/S único ─────────────────────────────────────────
// w: ganancia de side pedida (0=mono, 1=unity). En graves (f<150 Hz) se
// aplica el bassFactor del StereoWidener: w≤1 → w; w>1 → 1.75−0.75·w.
// Devuelve {sideGain, norm}: outL=(mid+side·sideGain)·norm, igual que el
// M/S de FusionCore (norm = 1/√(0.5(1+w²)), potencia constante).
struct WidthGains { float sideGain = 1.0f; float norm = 1.0f; };
inline WidthGains widthGains(float w, float freqHz) noexcept {
    if (!std::isfinite(w) || !std::isfinite(freqHz)) return WidthGains{};
    float ww = std::clamp(w, 0.0f, 3.0f);
    if (freqHz < 150.0f && ww > 1.0f) ww = std::max(1.75f - 0.75f * ww, 0.25f);
    WidthGains out;
    out.sideGain = ww;
    out.norm = 1.0f / std::sqrt(0.5f * (1.0f + ww * ww));
    return out;
}

// w_b desde el estado de la banda: fuentes difusas (coh baja) → side alto;
// fuentes puntuales (coh alta) → side bajo. widthScale = holos_width.
inline float widthFromState(float coherence, float widthScale) noexcept {
    if (!std::isfinite(coherence) || !std::isfinite(widthScale)) return 1.0f;
    const float coh = std::clamp(coherence, 0.0f, 1.0f);
    return std::clamp(widthScale * (0.3f + 0.7f * (1.0f - coh)), 0.0f, 1.5f);
}

// ── Γ_techo: softCeil + ganancia de pico del SafetyLimiter ────────────────
// softCeil: identidad ≤0.97·ceiling; racional (continua en valor y
// pendiente) por encima; nunca supera el ceiling. Copia exacta de
// SafetyLimiter::softCeil (dsp/SafetyLimiter.cpp).
inline float softCeil(float x, float ceiling = 0.98855f) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    const float knee = ceiling * 0.97f;
    const float ax = std::fabs(x);
    if (ax <= knee) return x;
    const float range = ceiling - knee;  // > 0
    const float over = (ax - knee) / range;
    const float y = knee + range * (over / (1.0f + over));
    return (x < 0.0f) ? -y : y;
}

// Ganancia objetivo (≤1) para un pico lineal: la curva de
// SafetyLimiter::computeGainForPeak (threshold −1 dBFS, ceiling −0.1 dBFS).
inline float limiterGainForPeak(float peakLin, float threshold = 0.891251f,
                                float ceiling = 0.98855f) noexcept {
    if (!std::isfinite(peakLin) || peakLin <= threshold) return 1.0f;
    const float excess = peakLin - threshold;
    float limited = threshold + excess / (1.0f + excess * 8.0f);
    if (limited > ceiling) limited = ceiling;
    return limited / peakLin;
}

}  // namespace holos
}  // namespace ivanna
