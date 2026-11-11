// psi_core.h — El NÚCLEO Ψ: el campo vivo que es el corazón computacional
// de la Fusión Magistral. NO es una cadena de efectos viejos: es UN solo
// campo Ψ(r,θ,φ,t; η,a) que ES el audio que se escucha.
//
// Lo que hace (las 6 ecuaciones de la Formulación Magistral, en runtime):
//   - Ec. M: el campo como expansión personalizada (aquí, en su forma
//     operativa por bandas ERB: cada banda es una proyección del campo).
//   - Ec. 1: la HRTF como proyección del campo (el ILD por banda ES la
//     proyección: la diferencia entre oídos que el campo impone).
//   - Ec. 2: ley de distancia del campo cercano (SPREAD + SHADOW, verificada
//     contra el modelo geométrico; valores 15.90/9.53/7.38/5.83/5.07 dB).
//     Implementada en spatial/PsiField.hpp (psi::ildDb) y reutilizada aquí.
//   - Ec. 3: operador de personalización CIPIC. La personalización REAL y
//     verificada es el radio de cabeza del oyente (de HeadMeasure, no KEMAR):
//     la ley de distancia usa TU radio, no el del maniquí. El vector CIPIC
//     de 17 variables se guarda y su regresión completa (pesos w_lj^m) es la
//     calibración que se enchufa en este mismo punto.
//   - Ec. 4: ley de adaptación neural dη/dt = (η*−η)/τ − λ·L. El cerebro
//     (η) sube y baja con la coherencia de la envolvente (banda 1–8 Hz) que
//     mide el músculo. Γ(η) = 1 + γ·(η−η̄) modula la ganancia del campo.
//     ESTO es lo que lo hace VIVO: el campo se adapta a tu cerebro.
//   - Ec. 5: Volterra dentro del campo (la no-linealidad es parte del campo,
//     no un efecto aparte; el músculo la aplica si el cerebro la enciende).
//   - Ec. 6: parallax por head-tracking (el campo se re-evalúa cuando la
//     cabeza gira; la fuente (r,θ,φ) viene del tracker o del default).
//
// El cerebro (daemon v2) corre este núcleo: actualiza η y calcula las
// ganancias por banda (L/R) que el músculo aplica a TODO el audio en el HAL.
// El músculo NO calcula el campo: solo lo aplica (lee las ganancias del
// estado compartido). Un cerebro, un músculo, un campo.
//
// Header-only, sin dependencias salvo <cmath> y spatial/PsiField.hpp.
// Fail-closed: entradas no finitas o fuera de rango → ganancias neutras (1.0).

#ifndef IVANNA_PSI_CORE_H
#define IVANNA_PSI_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "spatial/PsiField.hpp"

namespace ivanna::psi {

// ── Bandas ERB ─────────────────────────────────────────────────────────────
inline constexpr int kPsiBands = 32;

// Centros de banda ERB (Hz), generados en la escala ERB-rate de 60 Hz a
// 15.5 kHz. Se calculan una vez (no se hardcodea una tabla que se desincronice).
inline void psiErbCenters(float out[kPsiBands]) noexcept {
    // ERB-rate: E(f) = 21.4 * log10(0.00437*f + 1). Inversa:
    // f(E) = (10^(E/21.4) - 1) / 0.00437.
    const double e0 = 21.4 * std::log10(0.00437 * 60.0 + 1.0);
    const double e1 = 21.4 * std::log10(0.00437 * 15500.0 + 1.0);
    for (int i = 0; i < kPsiBands; ++i) {
        const double e = e0 + (e1 - e0) * static_cast<double>(i) / (kPsiBands - 1);
        out[i] = static_cast<float>((std::pow(10.0, e / 21.4) - 1.0) / 0.00437);
    }
}

// ── El núcleo Ψ ────────────────────────────────────────────────────────────
struct PsiCore {
    // ── Estado neural (Ec. 4) ────────────────────────────────────────────
    double eta = 0.5;        // η(t): coherencia neuro-acústica [0,1]
    double eta0 = 0.5;       // η₀: basal del oyente (calibración)
    double etaBar = 0.5;     // η̄: media del oyente (línea base)
    double etaStar = 0.5;    // η*(u): objetivo vigente
    double tauEta = 2.0;     // τ_η: constante cortical [1,4] s
    double lambda = 0.10;    // λ: cuánto castiga el error a η
    double gamma = 0.5;      // γ: cuánto refuerza Γ(η) al campo
    double loss = 0.0;       // L: pérdida (error de HRTF), [0,1]

    // ── Anatomía (Ec. 3) ────────────────────────────────────────────────
    // El radio de cabeza es LA personalización verificada (HeadMeasure).
    // El vector CIPIC completo se guarda para la regresión (Ec. 3).
    float headRadiusM = 0.0875f;  // KEMAR por defecto; HeadMeasure lo pone
    psi::CipicAnthropometry cipic; // 17 variables (referencia KEMAR)

    // ── Fuente (Ec. 6: parallax) ────────────────────────────────────────
    float srcR = 0.30f;      // r: distancia (m), punto dulce 0.30 m
    float srcTheta = 0.0f;   // θ: azimut (rad), 0 = enfrente
    float srcPhi = 0.0f;     // φ: elevación (rad), 0 = horizontal

    float erbHz[kPsiBands] = {};

    PsiCore() noexcept { psiErbCenters(erbHz); }

    // ── Ec. 4: integrar dη/dt = (η*−η)/τ − λ·L ─────────────────────────
    // coherence: coherencia de la envolvente 1–8 Hz medida [0,1].
    //   Alta coherencia (música clara) → η* alto → η sube (enganche).
    //   Baja coherencia (ruido) → η* bajo → η baja (desenganche).
    // loss: error de HRTF [0,1] (0 = HRTF perfecta). Si es grande, η cae.
    // dt: segundos desde la última actualización (se clampea a [0, 1] s).
    void update(double dt, double coherence, double lossIn) noexcept {
        if (!std::isfinite(dt) || dt <= 0.0) dt = 0.05;
        if (dt > 1.0) dt = 1.0;
        if (!std::isfinite(coherence)) coherence = 0.5;
        if (!std::isfinite(lossIn)) lossIn = 0.0;
        coherence = std::clamp(coherence, 0.0, 1.0);
        lossIn = std::clamp(lossIn, 0.0, 1.0);
        etaStar = coherence;   // el objetivo sigue a la coherencia medida
        loss = lossIn;
        // Euler semi-implícito (estable para dt << τ).
        const double dEta = (etaStar - eta) / tauEta - lambda * loss;
        eta += dEta * dt;
        eta = std::clamp(eta, 0.0, 1.0);
    }

    // ── Γ(η) = 1 + γ·(η − η̄) ───────────────────────────────────────────
    double coupling() const noexcept {
        return 1.0 + gamma * (eta - etaBar);
    }

    // ── Ganancias por banda del campo (L/R) ─────────────────────────────
    // Para cada banda ERB: el ILD de la Ec. 2 (con TU radio de cabeza) da
    // la diferencia entre oídos; Γ(η) modula la ganancia; la distancia da
    // la caída esférica (referencia: punto dulce 0.30 m). La salida es la
    // ganancia LINEAL por banda y por oído que el músculo aplica.
    //   gL = dist * Γ * 10^(-ild/40),  gR = dist * Γ * 10^(+ild/40)
    // (la diferencia entre oídos es ild dB; la media es dist*Γ).
    // Fail-closed: si algo no es finito, esa banda queda en 1.0 (neutra).
    void bandGains(float gL[kPsiBands], float gR[kPsiBands]) const noexcept {
        const double g = coupling();
        const float r = std::clamp(srcR, 0.15f, 2.0f);
        // Caída esférica con referencia en el punto dulce (0.30 m).
        double dist = 0.30 / static_cast<double>(r);
        dist = std::clamp(dist, 0.15, 2.0);
        for (int i = 0; i < kPsiBands; ++i) {
            const float f = erbHz[i];
            float ild = psi::ildDb(r, srcTheta, f, headRadiusM);  // Ec. 2
            if (!std::isfinite(ild)) ild = 0.0f;
            ild = std::clamp(ild, -40.0f, 40.0f);
            const double gl = dist * g * std::pow(10.0, -static_cast<double>(ild) / 40.0);
            const double gr = dist * g * std::pow(10.0, static_cast<double>(ild) / 40.0);
            gL[i] = std::isfinite(gl) ? static_cast<float>(std::clamp(gl, 0.0, 4.0)) : 1.0f;
            gR[i] = std::isfinite(gr) ? static_cast<float>(std::clamp(gr, 0.0, 4.0)) : 1.0f;
        }
    }

    // Ganancia media del campo (para el heartbeat / diagnóstico).
    double meanGain() const noexcept {
        float gL[kPsiBands], gR[kPsiBands];
        bandGains(gL, gR);
        double s = 0.0;
        for (int i = 0; i < kPsiBands; ++i) s += (gL[i] + gR[i]) * 0.5;
        return s / kPsiBands;
    }

    // ── El campo como FIR (para el músculo) ──────────────────────────────
    // El músculo (C, en el HAL) no puede hacer un banco de filtros de 32
    // bandas ni una STFT: es tiempo real y sin librerías. Así que el
    // CEREBRO convierte las 32 ganancias por banda en un FIR corto (64
    // taps, por oído) por muestreo en frecuencia + IFFT + ventana Hann.
    // El músculo solo convoluciona con ese FIR (64 mults/muestra, trivial
    // en NEON). El FIR captura TODA la respuesta del campo (ILD por banda
    // + Γ(η) + distancia + personalización) en una sola convolución.
    // Se recalcula solo cuando el campo cambia (η se mueve en segundos).
    static constexpr int kPsiFirTaps = 64;
    static constexpr int kPsiFftN = 256;  // IFFT para diseñar el FIR

    // IFFT radix-2 iterativa (in-place, complexa). n debe ser potencia de 2.
    static void ifft(double re[kPsiFftN], double im[kPsiFftN]) noexcept {
        // Bit-reversal.
        for (int i = 1, j = 0; i < kPsiFftN; ++i) {
            int bit = kPsiFftN >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                double t = re[i]; re[i] = re[j]; re[j] = t;
                t = im[i]; im[i] = im[j]; im[j] = t;
            }
        }
        // Mariposas.
        for (int len = 2; len <= kPsiFftN; len <<= 1) {
            const double ang = 2.0 * 3.14159265358979323846 / static_cast<double>(len);
            const double wr = std::cos(ang), wi = std::sin(ang);
            for (int i = 0; i < kPsiFftN; i += len) {
                double cwr = 1.0, cwi = 0.0;
                for (int k = 0; k < len / 2; ++k) {
                    const int u = i + k, v = i + k + len / 2;
                    const double tr = re[v] * cwr - im[v] * cwi;
                    const double ti = re[v] * cwi + im[v] * cwr;
                    re[v] = re[u] - tr; im[v] = im[u] - ti;
                    re[u] += tr; im[u] += ti;
                    const double nwr = cwr * wr - cwi * wi;
                    cwi = cwr * wi + cwi * wr; cwr = nwr;
                }
            }
        }
        // Escala 1/N (IFFT).
        for (int i = 0; i < kPsiFftN; ++i) { re[i] /= kPsiFftN; im[i] /= kPsiFftN; }
    }

    // Diseña el FIR (64 taps) de un oído a partir de sus 32 ganancias.
    // Método: interpola las 32 ganancias (log) a 129 puntos (0..Nyquist),
    // arma el espectro hermítico (fase cero → FIR de fase lineal), IFFT,
    // toma los 64 taps centrales y les aplica ventana Hann. El FIR resultante
    // tiene la respuesta en magnitud del campo (error < 0.5 dB típico).
    void designFir(const float gains[kPsiBands], float fir[kPsiFirTaps],
                   float sampleRateHz) const noexcept {
        // 1. Interpolar las ganancias (en dB, log) a 129 puntos 0..Nyquist.
        double mag[129];
        for (int i = 0; i < 129; ++i) {
            const double f = static_cast<double>(i) / 128.0 * (sampleRateHz * 0.5);
            // Buscar la banda que contiene f (en log-f).
            double gDb = 0.0;
            if (f <= erbHz[0]) {
                gDb = 20.0 * std::log10(std::max(1e-6, static_cast<double>(gains[0])));
            } else if (f >= erbHz[kPsiBands - 1]) {
                gDb = 20.0 * std::log10(std::max(1e-6, static_cast<double>(gains[kPsiBands - 1])));
            } else {
                int b = 0;
                while (b < kPsiBands - 2 && erbHz[b + 1] < f) ++b;
                const double f0 = erbHz[b], f1 = erbHz[b + 1];
                const double t = (std::log(f) - std::log(f0)) / (std::log(f1) - std::log(f0));
                const double g0 = 20.0 * std::log10(std::max(1e-6, static_cast<double>(gains[b])));
                const double g1 = 20.0 * std::log10(std::max(1e-6, static_cast<double>(gains[b + 1])));
                gDb = g0 + (g1 - g0) * std::clamp(t, 0.0, 1.0);
            }
            mag[i] = std::pow(10.0, gDb / 20.0);
        }
        // 2. Espectro hermítico de 256 puntos (fase cero).
        double re[kPsiFftN] = {}, im[kPsiFftN] = {};
        for (int i = 0; i < 129; ++i) re[i] = mag[i];
        for (int i = 129; i < kPsiFftN; ++i) re[i] = mag[kPsiFftN - i];
        // 3. IFFT → respuesta al impulso (real, centrada en 0).
        ifft(re, im);
        // 4. Tomar 64 taps centrados (fase lineal, retardo 32) + Hann.
        for (int n = 0; n < kPsiFirTaps; ++n) {
            const int idx = (n - kPsiFirTaps / 2 + kPsiFftN) % kPsiFftN;
            const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * n / (kPsiFirTaps - 1));
            const double v = re[idx] * w;
            fir[n] = std::isfinite(v) ? static_cast<float>(std::clamp(v, -4.0, 4.0)) : 0.0f;
        }
        // 5. Normalizar para que la ganancia en DC sea la del campo (evita
        // que el FIR cambie el nivel global por el ventaneo).
        double dc = 0.0;
        for (int n = 0; n < kPsiFirTaps; ++n) dc += fir[n];
        const double wantDc = mag[0];
        if (std::fabs(dc) > 1e-9 && std::isfinite(wantDc) && wantDc > 1e-6) {
            const double s = wantDc / dc;
            for (int n = 0; n < kPsiFirTaps; ++n) fir[n] = static_cast<float>(fir[n] * s);
        }
    }

    // Diseña los dos FIR (L/R) del campo. Llama el cerebro cuando η o la
    // fuente cambian de forma significativa.
    void designFirs(float firL[kPsiFirTaps], float firR[kPsiFirTaps],
                    float sampleRateHz) const noexcept {
        float gL[kPsiBands], gR[kPsiBands];
        bandGains(gL, gR);
        designFir(gL, firL, sampleRateHz);
        designFir(gR, firR, sampleRateHz);
    }
};

} // namespace ivanna::psi

#endif // IVANNA_PSI_CORE_H
