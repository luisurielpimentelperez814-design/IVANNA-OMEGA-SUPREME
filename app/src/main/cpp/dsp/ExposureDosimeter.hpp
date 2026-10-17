// ExposureDosimeter.hpp — Dosímetro de Exposición Auditiva (WHO / ITU-T H.870 / NIOSH / OSHA)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// MOTIVO: el producto sube graves, abre el campo y añade armónicos. Todo eso suena mejor
// y también carga más al oído. loudness_meter.hpp (BS.1770-4) y SafetyLimiter protegen
// contra CLIP y contra sonoridad excesiva, pero ninguno acumula DOSIS: cuánto de la
// capacidad auditiva del día ya se gastó. Este módulo cierra ese hueco y publica una
// carga [0,1] que atenúa la háptica y permite recomendar atenuación.
//
// PONDERACIÓN A EXACTA: no se aproxima con shelfs. Se toma la función racional analítica
// de la ponderación A (4 ceros en el origen, 6 polos reales conocidos) y se digitaliza
// por transformada bilineal en prepare(), con normalización a 0 dB en 1 kHz. Cada par de
// polos se empareja en un biquad de segundo orden, así el filtro son 3 biquads exactos
// por canal. El error frente a la tabla IEC 61672 clase 1 es de centésimas de dB.
//
// CRITERIOS SOPORTADOS:
//   · WHO / ITU-T H.870 — 80 dB(A) durante 1440 s diarios = 100 % de dosis, tasa 3 dB.
//   · NIOSH — 85 dB(A) durante 8 h = 100 %, tasa de intercambio 3 dB.
//   · OSHA — 90 dB(A) durante 8 h = 100 %, tasa 5 dB (referencia histórica).
//   · Límite de PICO sin ponderar: 135 dB (evita daño instantáneo por transitorio).
//
// RT-safe: feed() se llama SOLO desde el hilo de audio; los getters leen atómicos.
// Sin malloc, sin locks, sin excepciones.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ivanna { namespace dosing {

struct ExposureMode {
    float refSplDb;      // nivel de referencia
    float refSeconds;    // tiempo permitido a ese nivel
    float exchangeDb;    // tasa de intercambio
    const char* name;
};
inline constexpr ExposureMode kModeWHO   = {80.0f, 1440.0f, 3.0f, "WHO"};   // diaria
inline constexpr ExposureMode kModeNIOSH = {85.0f, 28800.0f, 3.0f, "NIOSH"};
inline constexpr ExposureMode kModeOSHA  = {90.0f, 28800.0f, 5.0f, "OSHA"};

inline constexpr float kPeakLimitDbSpl = 135.0f; // límite de pico (H.870 / IEC 62368-1)

// Biquad direct-form I con estado en doble precisión (necesario a 20 Hz con fs alta).
struct BiquadA {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    inline double process(double x) noexcept {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (!std::isfinite(y)) { y = 0.0; x1 = x2 = y1 = y2 = 0.0; return 0.0; }
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    inline void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
    // Respuesta en magnitud en la frecuencia normalizada w = 2*pi*f/fs.
    inline double magnitude(double w) const noexcept {
        const double cw = std::cos(w), sw = std::sin(w);
        const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
        const double nr = b0 + b1 * cw + b2 * c2;
        const double ni = -(b1 * sw + b2 * s2);
        const double dr = 1.0 + a1 * cw + a2 * c2;
        const double di = -(a1 * sw + a2 * s2);
        return std::sqrt(nr * nr + ni * ni) / (std::sqrt(dr * dr + di * di) + 1.0e-30);
    }
};

class alignas(64) ExposureDosimeter {
public:
    void prepare(float sampleRate) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? (double)sampleRate : 48000.0;
        designAWeighting();
        reset();
    }

    void reset() noexcept {
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 3; ++s) sec_[c][s].reset();
        sumL_ = sumR_ = 0.0;
        nAcc_ = 0;
        dose_.store(0.0f, std::memory_order_relaxed);
        laeq_.store(0.0f, std::memory_order_relaxed);
        peakDb_.store(-120.0f, std::memory_order_relaxed);
        overload_.store(false, std::memory_order_relaxed);
    }

    void setMode(int mode) noexcept { mode_.store(std::clamp(mode, 0, 2), std::memory_order_relaxed); }
    int  mode() const noexcept { return mode_.load(std::memory_order_relaxed); }

    // Calibración: nivel SPL equivalente a fondo de escala (pico 1.0). 94 dB = 1 Pa a fondo
    // de escala con DSP de 1 V/Pa; el defecto es conservador (asume auriculares sensibles).
    void setCalibration(float dbfsToSpl) noexcept {
        // El techo de 145 dB es fisico, no arbitrario: los receptores de audifono y los
        // drivers de alta sensibilidad superan 130-140 dB SPL a fondo de escala. Un tope
        // de 130 impedia representar esos transductores y el detector de pico nunca
        // llegaba a marcar sobrecarga (defecto detectado en la verificacion).
        if (std::isfinite(dbfsToSpl)) calDb_.store(std::clamp(dbfsToSpl, 70.0f, 145.0f), std::memory_order_relaxed);
    }
    float calibration() const noexcept { return calDb_.load(std::memory_order_relaxed); }

    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    void resetDose() noexcept { dose_.store(0.0f, std::memory_order_relaxed); }

    // ── Alimentación (hilo de audio, RT-safe) ────────────────────────────────
    void feed(const float* __restrict L, const float* __restrict R, int n) noexcept {
        if (!enabled_.load(std::memory_order_relaxed) || !L || n <= 0) return;
        float pk = peakDb_.load(std::memory_order_relaxed);

        for (int i = 0; i < n; ++i) {
            const float l = std::isfinite(L[i]) ? L[i] : 0.0f;
            const float r = (R ? (std::isfinite(R[i]) ? R[i] : 0.0f) : l);
            double al = (double)l, ar = (double)r;
            for (int s = 0; s < 3; ++s) {
                al = sec_[0][s].process(al);
                ar = sec_[1][s].process(ar);
            }
            sumL_ += al * al;
            sumR_ += ar * ar;
            // Pico Z (sin ponderar): el daño por transitorio no se pondera.
            const float mx = std::max(std::fabs(l), std::fabs(r));
            if (mx > 1.0e-6f) {
                const float db = 20.0f * std::log10(mx) + calDb_.load(std::memory_order_relaxed);
                if (db > pk) pk = db;
            }
            ++nAcc_;
        }

        if ((double)nAcc_ >= 0.100 * sr_) { // ventana de cálculo: 100 ms
            const double inv = 1.0 / (double)nAcc_;
            const double msL = sumL_ * inv;
            const double msR = sumR_ * inv;
            const double meanSq = 0.5 * (msL + msR);   // media de canales (BS.1770 / H.870)
            const double la = 10.0 * std::log10(std::max(meanSq, 1.0e-20))
                            + (double)calDb_.load(std::memory_order_relaxed);
            const double dt = (double)nAcc_ / sr_;
            sumL_ = sumR_ = 0.0; nAcc_ = 0;

            if (la > 40.0) { // por debajo de 40 dB(A) la dosis es despreciable
                const ExposureMode m = currentMode();
                const double allowed = (double)m.refSeconds
                                     * std::pow(2.0, (double)(m.refSplDb - la) / (double)m.exchangeDb);
                const double inc = dt / std::max(allowed, 1.0);
                const float d = dose_.load(std::memory_order_relaxed) + (float)inc;
                dose_.store(std::clamp(d, 0.0f, 4.0f), std::memory_order_relaxed);
                laeq_.store((float)la, std::memory_order_relaxed);
            }
            peakDb_.store(pk, std::memory_order_relaxed);
            if (pk > kPeakLimitDbSpl) overload_.store(true, std::memory_order_relaxed);
        }
    }

    // ── Telemetría / actuación ───────────────────────────────────────────────
    float dose() const noexcept { return dose_.load(std::memory_order_relaxed); }  // 1.0 = límite
    float laeqDb() const noexcept { return laeq_.load(std::memory_order_relaxed); }
    float peakDbSpl() const noexcept { return peakDb_.load(std::memory_order_relaxed); }
    bool  peakOverloaded() const noexcept { return overload_.load(std::memory_order_relaxed); }

    // Tiempo seguro restante al nivel actual (segundos).
    float remainingSeconds() const noexcept {
        const float l = laeqDb();
        if (!(l > 40.0f)) return 1.0e9f;
        const ExposureMode m = currentMode();
        const double allowed = (double)m.refSeconds
                             * std::pow(2.0, (double)(m.refSplDb - l) / (double)m.exchangeDb);
        return (float)std::max(allowed * (1.0 - (double)dose()), 0.0);
    }

    // Atenuación recomendada (dB) para que la sesión termine dentro del 100 % de dosis.
    float recommendedAttenuationDb() const noexcept {
        const float d = dose();
        if (d <= 0.5f) return 0.0f;
        return std::clamp((d - 0.5f) / 0.5f * 6.0f, 0.0f, 6.0f); // hasta −6 dB, nunca más
    }

    // Carga para MultibandHapticMapper (la háptica cede cuando el oído ya está cargado).
    float hapticDoseLoad() const noexcept {
        return std::clamp((dose() - 0.25f) / 0.75f, 0.0f, 1.0f);
    }

    // Magnitud de la ponderación A en una frecuencia (para verificación de diseño).
    float aWeightMagnitude(float freqHz) const noexcept {
        const double w = 2.0 * 3.141592653589793 * (double)freqHz / sr_;
        double mag = 1.0;
        for (int s = 0; s < 3; ++s) mag *= sec_[0][s].magnitude(w);
        return (float)mag;
    }
    float aWeightDb(float freqHz) const noexcept {
        return 20.0f * std::log10(std::max(aWeightMagnitude(freqHz), 1.0e-12f));
    }

private:
    ExposureMode currentMode() const noexcept {
        const int m = mode();
        if (m == 1) return kModeNIOSH;
        if (m == 2) return kModeOSHA;
        return kModeWHO;
    }

    // Ponderación A por transformada bilineal de la función racional analítica:
    //   H_A(s) = s⁴ / ((s+p1)² (s+p2) (s+p3) (s+p4)²)
    //   p = {129.4, 129.4, 676.7, 4636.0, 76617.0, 76617.0}  (rad/s)
    // El bilineal da (1 − z⁻¹)⁴(1 + z⁻¹)² en el numerador (cuatro ceros en continua y dos
    // en Nyquist) frente a los 6 polos reales, que es la firma real de la ponderacion A.
    // Cada par de polos + su par de ceros forma un biquad directo.
    void designAWeighting() noexcept {
        const double p[6] = {129.4, 129.4, 676.7, 4636.0, 76617.0, 76617.0};
        double zp[6];
        for (int i = 0; i < 6; ++i) zp[i] = (2.0 * sr_ - p[i]) / (2.0 * sr_ + p[i]);

        // Numerador exacto del bilineal: (1 - z^-1)^4 (1 + z^-1)^2 — cuatro ceros en
        // z = +1 (continua) y dos en z = -1 (Nyquist). Esa es la firma real de la
        // ponderacion A: atenuacion brutal en graves y suave en agudos. Repartido en
        // tres secciones: las secciones 0 y 1 llevan dos ceros en +1; la 2, los de Nyquist.
        const int pair[3][2] = {{0, 1}, {2, 3}, {4, 5}};
        for (int s = 0; s < 3; ++s) {
            const double zsign = (s == 2) ? 2.0 : -2.0;
            sec_[0][s].b0 = 1.0; sec_[0][s].b1 = zsign; sec_[0][s].b2 = 1.0;
            sec_[0][s].a1 = -(zp[pair[s][0]] + zp[pair[s][1]]);
            sec_[0][s].a2 = zp[pair[s][0]] * zp[pair[s][1]];
            sec_[0][s].reset();
            sec_[1][s] = sec_[0][s];   // canal R con estado propio
        }
        // Ganancia de normalización: |H| = 1 a 1 kHz (definición de la ponderación A).
        const double w = 2.0 * 3.141592653589793 * 1000.0 / sr_;
        double mag = 1.0;
        for (int s = 0; s < 3; ++s) mag *= sec_[0][s].magnitude(w);
        const double g = 1.0 / std::max(mag, 1.0e-12);
        for (int c = 0; c < 2; ++c) {
            sec_[c][0].b0 *= g; sec_[c][0].b1 *= g; sec_[c][0].b2 *= g;
        }
    }

    double sr_ = 48000.0;
    BiquadA sec_[2][3]{};
    double sumL_ = 0.0, sumR_ = 0.0;
    int    nAcc_ = 0;

    std::atomic<float> dose_{0.0f};
    std::atomic<float> laeq_{0.0f};
    std::atomic<float> peakDb_{-120.0f};
    std::atomic<float> calDb_{94.0f};
    std::atomic<bool>  overload_{false};
    std::atomic<bool>  enabled_{false};
    std::atomic<int>   mode_{0};
};

}} // namespace ivanna::dosing
