// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// HolosStateEstimator — s_b: el vector de estado por banda de HOLOS.
//
// FUSION.md §4.5: UNA sola estimación por frame (hop 128), tres usos
// (G, K, Γ). Es el sentido literal de "mezcla homogénea".
//
//   s_b(t) = ( θ_b, r_b, coh_b, DRR_b, env_b, lvl_b, tr_b )
//
// | Componente | Origen legacy | Estimación en HOLOS |
// | θ_b, r_b   | ORBE (ILD/IPD/coherencia, bisección 24 iter) | Igual, sin cambios |
// | coh_b      | ORBE (\|X\|/√(pL·pR)) | Igual; gobierna directo vs difuso |
// | DRR_b      | (ORBE no la calcula aparte) | Proxy desde coherencia:
// |            | | DRR = 10·log10(coh/(1−coh)) clamp ±20 dB. Se estima y se
// |            | | expone; su uso como modulador de Γ_dinámico queda para
// |            | | fase 2 (el balance directo/difuso ya lo gobierna coh_b).
// |            | | No se inventa un mapeo sin medirlo. |
// | env_b      | PDEngine + coclear Heun + detector compresor | UN solo
// |            | | seguidor: Heun (RK2) con ataque 10 ms / release 80 ms
// |            | | (las constantes fisiológicas del coclear), sobre \|X_b\|,
// |            | | actualizado 1×/frame (hop 128). |
// | lvl_b      | Compresor (2 etapas) + limiter (true-peak) | Pico de banda
// |            | | (máx \|bin\|), ataque inmediato, release 50 ms. |
// | tr_b       | TransientDetector (ratio>3) + PhaseOracle | Ratio
// |            | | envFast/envSlow sobre \|X_b\| (5 ms / 150 ms, umbral 3.0);
// |            | | en ataque, el azimut se estrecha ×0.55 (como el upmixer). |
//
// Header-only, sin alloc en estimate() (tablas de bandas prestadas por
// HolosOperator, que es quien las posee). Estado por instancia.
// La bisección de azimut/distancia es la misma de OrbeBinaural (24 iter,
// NearFieldHeadModel): si el estimador unificado no coincide con ORBE
// dentro de tolerancia, el estimador está mal y se para aquí (FUSION §10
// paso 2).
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>

#include "NearFieldHeadModel.hpp"

namespace ivanna {
namespace holos {

inline constexpr float kHolosPiF = 3.14159265358979323846f;
inline constexpr int kHolosMaxBands = 24;

struct HolosBandState {
    float theta = 0.0f;      // azimut estimado (rad, + = derecha)
    float r = 0.35f;         // distancia estimada (m)
    float coh = 1.0f;        // coherencia interaural [0,1]
    float drrDb = 0.0f;      // DRR proxy (dB, clamp ±20)
    float env = 0.0f;        // envolvente Heun de |X_b| (lineal)
    float lvl = 0.0f;        // pico de banda (lineal)
    bool transient = false;  // ataque detectado este frame
};

class HolosStateEstimator {
public:
    static constexpr int kHop = 128;

    void prepare(float sr) noexcept {
        sampleRate_ = std::isfinite(sr) ? std::clamp(sr, 8000.0f, 384000.0f) : 48000.0f;
        reset();
    }

    void reset() noexcept {
        meanCoherence_ = 1.0f;
        for (int b = 0; b < kHolosMaxBands; ++b) {
            state_[b] = HolosBandState{};
            state_[b].r = distanceM_;
            envFast_[b] = 0.0f;
            envSlow_[b] = 0.0f;
            trHold_[b] = 0;
            xReS_[b] = 0.0;
            xImS_[b] = 0.0;
            pLS_[b] = 0.0;
            pRS_[b] = 0.0;
        }
    }

    void setParams(float distanceM, float spread) noexcept {
        distanceM_ = std::isfinite(distanceM) ? std::clamp(distanceM, 0.15f, 2.0f) : 0.35f;
        spread_ = std::isfinite(spread) ? std::clamp(spread, 0.0f, 1.5f) : 1.0f;
    }

    // Tablas de bandas activas (prestadas: las posee HolosOperator).
    // loBin/hiBin/centerHz tienen `count` entradas (12 o 24).
    void setBandTables(const int* loBin, const int* hiBin, const float* centerHz,
                       int count) noexcept {
        loBin_ = loBin;
        hiBin_ = hiBin;
        centerHz_ = centerHz;
        bandCount_ = (count == 12) ? 12 : 24;
    }

    int bandCount() const noexcept { return bandCount_; }
    float meanCoherence() const noexcept { return meanCoherence_; }
    const HolosBandState& state(int b) const noexcept {
        static const HolosBandState kEmpty{};
        if (b < 0 || b >= bandCount_) return kEmpty;
        return state_[b];
    }

    // Estima s_b para un frame a partir de los espectros (re/im, L/R).
    // n = fftSize (los espectros tienen n bins; solo se usan 0..n/2).
    void estimate(const float* reL, const float* imL, const float* reR, const float* imR,
                  int n) noexcept {
        if (reL == nullptr || imL == nullptr || reR == nullptr || imR == nullptr ||
            loBin_ == nullptr || hiBin_ == nullptr || centerHz_ == nullptr || n <= 0)
            return;
        const float sr = sampleRate_;
        const float hopSec = (float)kHop / sr;
        // Suavizado de escena ~20 ms (como ORBE).
        const float alpha = 1.0f - std::exp(-hopSec / 0.020f);
        // Envolvente Heun por frame: ataque 10 ms / release 80 ms.
        const float attC = 1.0f - std::exp(-hopSec / 0.010f);
        const float relC = 1.0f - std::exp(-hopSec / 0.080f);
        // Transientes: envFast 5 ms / envSlow 150 ms.
        const float fastC = 1.0f - std::exp(-hopSec / 0.005f);
        const float slowC = 1.0f - std::exp(-hopSec / 0.150f);
        // Release del pico de banda: 50 ms.
        const float lvlRel = std::exp(-hopSec / 0.050f);
        const float spread = spread_;
        const float distParam = distanceM_;
        float cohSum = 0.0f;
        for (int b = 0; b < bandCount_; ++b) {
            const int lo = loBin_[b], hi = hiBin_[b];
            const float fc = centerHz_[b];
            double pL = 0.0, pR = 0.0, xRe = 0.0, xIm = 0.0;
            float peak = 0.0f;
            for (int k = lo; k < hi && k <= n / 2; ++k) {
                const double lRe = reL[k], lIm = imL[k], rRe = reR[k], rIm = imR[k];
                pL += lRe * lRe + lIm * lIm;
                pR += rRe * rRe + rIm * rIm;
                xRe += lRe * rRe + lIm * rIm;   // cross = conj(L)·R
                xIm += lRe * rIm - lIm * rRe;
                const float mL = (float)std::sqrt(lRe * lRe + lIm * lIm);
                const float mR = (float)std::sqrt(rRe * rRe + rIm * rIm);
                peak = std::max(peak, std::max(mL, mR));
            }
            const int bins = std::max(1, hi - lo);
            // ── Amplitud lineal de la banda (Parseval, ventana Hann) ──
            // D9 (bug encontrado por test_holos_operator): mag era la
            // magnitud FFT cruda (~n/4 × la amplitud temporal) y Γ la
            // leía como dBFS → un tono de 0.5 (−6 dBFS) se veía como
            // +36 dBFS y el compresor lo aplastaba −29 dB. Para un tono
            // de amplitud A, Σ|Y|² (un canal, frec. positivas) =
            // 3n²A²/32 (Parseval, Σw²=3n/8) → A = √(32·p/(3n²)); con
            // dos canales se promedia la potencia: A=√(16·(pL+pR)/(3n²)).
            // Es independiente del ancho de banda (suma potencia, no
            // promedia bins) y da la amplitud equivalente en seno para
            // ruido (√2·RMS de la banda). El pico de banda se normaliza
            // igual (bin pico = A·n/4, con pérdida de scalloping 0.85–1).
            const float mag = (float)std::sqrt(16.0 * (pL + pR) / (3.0 * (double)n * (double)n));
            (void)bins;
            HolosBandState& s = state_[b];

            // ── Envolvente Heun (RK2) por frame ──
            const float hc = (mag > s.env) ? attC : relC;
            const float k1 = (mag - s.env) * hc;
            const float pred = s.env + k1;
            const float k2 = (mag - pred) * hc;
            s.env += 0.5f * (k1 + k2);
            if (!std::isfinite(s.env)) s.env = 0.0f;
            // ── Pico de banda (amplitud lineal, misma escala que mag) ──
            s.lvl = std::max(peak * (4.0f / (float)n), s.lvl * lvlRel);
            // ── Transiente (ratio envFast/envSlow) ──
            envFast_[b] += fastC * (mag - envFast_[b]);
            envSlow_[b] += slowC * (mag - envSlow_[b]);
            const bool trNow = (envSlow_[b] > 1e-9f) && (envFast_[b] / envSlow_[b] > 3.0f) &&
                               (envFast_[b] > 1e-4f);
            if (trNow) trHold_[b] = 5;  // hold de 5 frames (~13 ms)
            s.transient = trHold_[b] > 0;
            if (trHold_[b] > 0) --trHold_[b];

            // ── Coherencia del cross-espectro COMPLEJO suavizado ──
            // D10 (bug encontrado por test_holos_operator): se promediaba
            // la MAGNITUD de la coherencia por frame, lo que sesga
            // coh → 1/√bins con ruido incorrelado (medido 0.62 en vez
            // de ~0) y roba el término difuso. Se suavizan el
            // cross-espectro complejo y las potencias (20 ms) y la
            // coherencia se calcula DEL PROMEDIO (estimador tipo Welch):
            // el ruido incorrelado cancela vectorialmente y coh → ~0,
            // mientras una señal coherente suma en fase y coh → 1.
            xReS_[b] += alpha * (xRe - xReS_[b]);
            xImS_[b] += alpha * (xIm - xImS_[b]);
            pLS_[b] += alpha * (pL - pLS_[b]);
            pRS_[b] += alpha * (pR - pRS_[b]);

            // ── Escena (ORBE, sin cambios) ──
            float coh = s.coh;
            if (pL + pR > 1e-10) {
                const double denom = std::sqrt(pLS_[b] * pRS_[b]);
                coh = denom > 1e-12
                          ? (float)(std::sqrt(xReS_[b] * xReS_[b] + xImS_[b] * xImS_[b]) / denom)
                          : 0.0f;
                coh = std::clamp(coh, 0.0f, 1.0f);
                const float ildMeas = 10.0f * (float)std::log10((pR + 1e-12) / (pL + 1e-12));
                const float ipd = (float)std::atan2(xIm, xRe);
                const float itdMeas = std::clamp(ipd / (2.0f * kHolosPiF * fc), -0.0008f, 0.0008f);
                const float rCur = s.r;
                float thetaEst;
                if (fc < 700.0f) {
                    thetaEst = thetaFromItd(itdMeas, rCur);
                } else {
                    thetaEst = thetaFromIld(ildMeas, rCur, fc);
                }
                // En ataque el azimut se estrecha ×0.55 (como el upmixer).
                // No hay realimentación: el transiente depende solo de la
                // envolvente, no de θ_b.
                const float trScale = s.transient ? 0.55f : 1.0f;
                thetaEst = std::clamp(thetaEst * spread * trScale, -kHolosPiF * 0.5f,
                                      kHolosPiF * 0.5f);
                s.theta += alpha * (thetaEst - s.theta);
                if (fc < 800.0f && coh > 0.5f) {
                    const float rEst = distanceFromIld(ildMeas, thetaEst, fc);
                    s.r += alpha * (rEst - s.r);
                } else {
                    s.r += alpha * (distParam - s.r);
                }
            }
            // s.coh es ya suave: el suavizado vive en las entradas
            // (cross complejo + potencias), no en el cociente.
            s.coh = coh;
            // DRR proxy desde coherencia (ver cabecera).
            const float c = std::clamp(s.coh, 0.001f, 0.999f);
            s.drrDb = std::clamp(10.0f * std::log10(c / (1.0f - c)), -20.0f, 20.0f);
            cohSum += s.coh;
        }
        meanCoherence_ = bandCount_ > 0 ? cohSum / (float)bandCount_ : 1.0f;
    }

    float thetaFromItd(float tauSeconds, float r) const noexcept {
        const float a = std::fabs(tauSeconds);
        if (a <= 1e-9f) return 0.0f;
        const float sign = (tauSeconds > 0.0f) ? 1.0f : -1.0f;
        const float hi = kHolosPiF * 0.5f;
        if (nearfield::itdSeconds(hi, r) <= a) return sign * hi;
        float lo = 0.0f, hiB = hi;
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (lo + hiB);
            if (nearfield::itdSeconds(mid, r) < a) lo = mid; else hiB = mid;
        }
        return sign * 0.5f * (lo + hiB);
    }

    float thetaFromIld(float ildDbMeas, float r, float freqHz) const noexcept {
        const float a = std::fabs(ildDbMeas);
        if (a <= 1e-6f) return 0.0f;
        const float sign = (ildDbMeas > 0.0f) ? 1.0f : -1.0f;
        const float hi = kHolosPiF * 0.5f;
        if (nearfield::ildDb(hi, r, freqHz) <= a) return sign * hi;
        float lo = 0.0f, hiB = hi;
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (lo + hiB);
            if (nearfield::ildDb(mid, r, freqHz) < a) lo = mid; else hiB = mid;
        }
        return sign * 0.5f * (lo + hiB);
    }

    float distanceFromIld(float ildDbMeas, float theta, float freqHz) const noexcept {
        const float a = std::fabs(ildDbMeas);
        const float th = std::fabs(theta);
        if (th < 0.05f) return distanceM_;  // sin info de ILD en el centro
        if (a <= nearfield::ildDb(th, 2.0f, freqHz)) return 2.0f;
        if (a >= nearfield::ildDb(th, 0.15f, freqHz)) return 0.15f;
        float lo = 0.15f, hi = 2.0f;  // ildDb decrece con r
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (nearfield::ildDb(th, mid, freqHz) > a) lo = mid; else hi = mid;
        }
        return 0.5f * (lo + hi);
    }

private:
    float sampleRate_ = 48000.0f;
    float distanceM_ = 0.35f;
    float spread_ = 1.0f;
    const int* loBin_ = nullptr;
    const int* hiBin_ = nullptr;
    const float* centerHz_ = nullptr;
    int bandCount_ = 24;
    HolosBandState state_[kHolosMaxBands];
    float envFast_[kHolosMaxBands] = {};
    float envSlow_[kHolosMaxBands] = {};
    int trHold_[kHolosMaxBands] = {};
    // Estado suavizado para la coherencia (D10): cross-espectro complejo
    // y potencias por banda, EMA 20 ms. Se resetean en reset().
    double xReS_[kHolosMaxBands] = {};
    double xImS_[kHolosMaxBands] = {};
    double pLS_[kHolosMaxBands] = {};
    double pRS_[kHolosMaxBands] = {};
    float meanCoherence_ = 1.0f;
};

}  // namespace holos
}  // namespace ivanna
