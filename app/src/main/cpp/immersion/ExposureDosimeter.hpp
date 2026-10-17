// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================================
 * IVANNA OMEGA SUPREME — ExposureDosimeter (immersion/)
 * ============================================================================
 *
 * Dosímetro de exposición auditiva para sesiones de escucha intensas. Una
 * experiencia envolvente que sube la presión sonora sin medida termina dañando
 * el oído; este módulo la mide y le pone techo.
 *
 * Método (criterio NIOSH, 85 dBA durante 8 h = 100 % de dosis, cambio de 3 dB):
 *   1. Filtro de ponderación A (IEC 61672: polos 20.6, 107.7, 737.9, 12194 Hz,
 *      bilineal con prewarp a 1 kHz, normalizado a 0 dB en 1 kHz).
 *   2. Nivel por bloque:  LAeq = splAtFullScale + 10·log10(ms) + 3.01
 *      (un seno a escala completa lee exactamente splAtFullScale).
 *   3. Dosis:  Δdosis = dt / T(L),  T(L) = 8 h / 2^((L − 85) / 3);
 *      por debajo de 70 dBA no se acumula.
 *   4. Pronóstico: con el nivel rodante de ~30 s, ¿cuánta dosis habría al
 *      terminar la sesión planeada? Si pasa de 100 %, sugiere una atenuación
 *      (dB) con límite de velocidad de 1 dB/s, y una "licencia de realce" que
 *      baja los techos de armónicos/háptica del ArousalConductor.
 *
 * LÍMITE HONESTO — esto es una ESTIMACIÓN, no un instrumento certificado:
 *   la presión real depende del volumen del teléfono, del amplificador del
 *   Genezi y del transductor, que el código no conoce. `setCalibration()`
 *   recibe cuánto SPL produce un seno a escala completa en ESE equipo; hasta
 *   que se mida con un sonómetro, el valor por defecto (100 dB) es una cota
 *   conservadora, no una medición.
 *
 * Sin malloc, sin hilos; entradas no finitas se ignoran.
 * ============================================================================
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace ivanna::immersion {

class ExposureDosimeter {
public:
    static constexpr double kCriterionDba   = 85.0;
    static constexpr double kCriterionHours = 8.0;
    static constexpr double kExchangeDb     = 3.0;
    static constexpr double kThresholdDba   = 70.0;
    static constexpr float  kMaxAttenuationDb = 12.0f;
    static constexpr float  kAttenSlewDbPerSec = 1.0f;

    void prepare(float sampleRate, float sessionPlanMinutes = 120.0f) noexcept {
        fs_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? double(sampleRate) : 48000.0;
        sessionSec_ = (std::isfinite(sessionPlanMinutes) && sessionPlanMinutes > 0.0f)
                          ? double(std::min(sessionPlanMinutes, 1440.0f)) * 60.0 : 7200.0;
        designAWeighting();
        reset();
    }

    void reset() noexcept {
        for (auto& s : st_) s = {0.0, 0.0};
        dose_ = 0.0;
        rollMs_ = 0.0;
        rollInit_ = false;
        attenDb_ = 0.0f;
        lastLevelDba_ = 0.0;
        elapsedSec_ = 0.0;
    }

    /** Presión (dB SPL) que produce un seno de amplitud 1.0 en este equipo. */
    void setCalibration(float splAtFullScaleDb) noexcept {
        if (std::isfinite(splAtFullScaleDb)) calDb_ = std::clamp(double(splAtFullScaleDb), 60.0, 140.0);
    }
    void setSessionPlanMinutes(float minutes) noexcept {
        if (std::isfinite(minutes) && minutes > 0.0f) sessionSec_ = double(std::min(minutes, 1440.0f)) * 60.0;
    }

    /** Procesa un bloque mono ya renderizado (suma L+R * 0.5 hecha por el llamador). */
    void process(const float* x, std::size_t n) noexcept {
        if (x == nullptr || n == 0) return;
        double ms = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double v = std::isfinite(x[i]) ? double(x[i]) : 0.0;
            for (auto& s : st_) {
                // DF2T por sección
                const std::size_t k = std::size_t(&s - st_.data());
                const double y = sec_[k].b0 * v + s[0];
                s[0] = sec_[k].b1 * v - sec_[k].a1 * y + s[1];
                s[1] = sec_[k].b2 * v - sec_[k].a2 * y;
                v = y;
            }
            v *= gain_;
            ms += v * v;
        }
        ms /= double(n);
        const double dt = double(n) / fs_;
        const double level = levelFromMeanSquare(ms);
        accumulate(level, dt);
        updateRolling(ms, dt);
        updateAdvice(dt);
    }

    /** Acumula dosis directamente desde un nivel (dBA) y una duración (s). */
    void accumulate(double levelDba, double seconds) noexcept {
        lastLevelDba_ = levelDba;
        elapsedSec_ += seconds;
        if (!(levelDba >= kThresholdDba) || !(seconds > 0.0)) return;
        const double allowedSec = kCriterionHours * 3600.0 / std::pow(2.0, (levelDba - kCriterionDba) / kExchangeDb);
        dose_ += seconds / allowedSec;
    }

    double doseFraction() const noexcept { return dose_; }
    double lastLevelDba() const noexcept { return lastLevelDba_; }
    float suggestedAttenuationDb() const noexcept { return attenDb_; }

    /** Nivel rodante (dBA, ~30 s). */
    double rollingLevelDba() const noexcept { return rollInit_ ? levelFromMeanSquare(rollMs_) : 0.0; }

    /** Dosis proyectada al terminar la sesión planeada si el nivel rodante se mantiene. */
    double projectedFraction() const noexcept {
        const double remaining = std::max(0.0, sessionSec_ - elapsedSec_);
        double d = dose_;
        const double lvl = rollingLevelDba();
        if (lvl >= kThresholdDba) {
            d += remaining / (kCriterionHours * 3600.0 / std::pow(2.0, (lvl - kCriterionDba) / kExchangeDb));
        }
        return d;
    }

    /**
     * Licencia de realce 0..1: 1 con margen (proyección <= 80 %), baja
     * linealmente a 0 en 120 %. El integrador la usa para bajar los techos de
     * armónicos/háptica del ArousalConductor.
     */
    float boostAllowance() const noexcept {
        const double p = projectedFraction();
        return float(std::clamp((1.2 - p) / 0.4, 0.0, 1.0));
    }

    /** Respuesta del filtro A (dB) — expuesta para verificarla contra IEC 61672. */
    double responseDb(double hz) const noexcept {
        const double w = 2.0 * 3.14159265358979323846 * hz / fs_;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        std::complex<double> h(gain_, 0.0);
        for (const auto& s : sec_) {
            h *= (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
        }
        return 20.0 * std::log10(std::abs(h) + 1e-30);
    }

private:
    struct Sec { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    double levelFromMeanSquare(double ms) const noexcept {
        // +3.01 dB: un seno de amplitud 1 tiene ms = 0.5 y debe leer calDb_.
        return calDb_ + 10.0 * std::log10(std::max(ms, 1e-20)) + 3.0103;
    }

    void updateRolling(double ms, double dt) noexcept {
        if (!rollInit_) { rollMs_ = ms; rollInit_ = true; return; }
        const double a = 1.0 - std::exp(-dt / 30.0);
        rollMs_ += a * (ms - rollMs_);
    }

    void updateAdvice(double dt) noexcept {
        const double p = projectedFraction();
        float target = 0.0f;
        if (p > 1.0) target = float(std::min(double(kMaxAttenuationDb), kExchangeDb * std::log2(p)));
        const float maxStep = kAttenSlewDbPerSec * float(dt);
        if (target > attenDb_) attenDb_ = std::min(target, attenDb_ + maxStep);
        else attenDb_ = std::max(target, attenDb_ - maxStep);
    }

    // Sección bilineal desde H(s) = (n2 s² + n1 s + n0) / (d2 s² + d1 s + d0)
    static Sec bilinear(double n2, double n1, double n0, double d2, double d1, double d0, double c) noexcept {
        const double c2 = c * c;
        const double nb0 = n2 * c2 + n1 * c + n0, nb1 = -2.0 * n2 * c2 + 2.0 * n0, nb2 = n2 * c2 - n1 * c + n0;
        const double da0 = d2 * c2 + d1 * c + d0, da1 = -2.0 * d2 * c2 + 2.0 * d0, da2 = d2 * c2 - d1 * c + d0;
        Sec s;
        s.b0 = nb0 / da0; s.b1 = nb1 / da0; s.b2 = nb2 / da0;
        s.a1 = da1 / da0; s.a2 = da2 / da0;
        return s;
    }

    void designAWeighting() noexcept {
        const double pi = 3.14159265358979323846;
        const double w1 = 2 * pi * 20.598997, w2 = 2 * pi * 107.65265, w3 = 2 * pi * 737.86223, w4 = 2 * pi * 12194.217;
        const double wr = 2.0 * pi * 1000.0;
        const double c = wr / std::tan(wr / (2.0 * fs_));  // prewarp: coincide exacto en 1 kHz
        sec_[0] = bilinear(1, 0, 0, 1, 2 * w1, w1 * w1, c);
        sec_[1] = bilinear(1, 0, 0, 1, w2 + w3, w2 * w3, c);
        sec_[2] = bilinear(0, 0, 1, 1, 2 * w4, w4 * w4, c);
        gain_ = 1.0;
        gain_ = std::pow(10.0, -responseDb(1000.0) / 20.0);  // 0 dB en 1 kHz
    }

    double fs_ = 48000.0, sessionSec_ = 7200.0, calDb_ = 100.0;
    double gain_ = 1.0;
    std::array<Sec, 3> sec_{};
    std::array<std::array<double, 2>, 3> st_{};
    double dose_ = 0.0, rollMs_ = 0.0, lastLevelDba_ = 0.0, elapsedSec_ = 0.0;
    bool rollInit_ = false;
    float attenDb_ = 0.0f;
};

}  // namespace ivanna::immersion
