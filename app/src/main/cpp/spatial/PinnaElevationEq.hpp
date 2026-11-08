// =====================================================================
//  IVANNA — EQ de elevación por pinna (bandas direccionales de Blauert)
//  PinnaElevationEq.hpp — header-only, C++20/26, cero malloc en audio.
//  v2.5.368
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  El módulo que faltaba para el pilar 4 (docs/AUX_GENEZI_ROUTE.md §4).
//  En Ruta B el wet HRTF binaural se fuerza a 0 con AUX activo, pero
//  las pistas de elevación NO necesitan HRTF binaural: Blauert (1997,
//  "Spatial Hearing", pp. 108-111) demostró que ciertas bandas de
//  frecuencia del espectro se asocian a direcciones en el plano medio,
//  independientemente de la posición física del altavoz:
//
//      frente:  300-500 Hz y 3-4 kHz
//      atrás:   1 kHz (y 10-12 kHz)
//      arriba:  8 kHz
//
//  (Lee 2015, ICSA; Hebrank & Wright 1974; Clarke 2019.) Subir la
//  energía relativa de la banda de 8 kHz aumenta la "arribidad" de la
//  imagen fantasma; más energía en 4 kHz, más "frontalidad"; más en
//  1 kHz, más "atrasidad".
//
//  Este módulo aplica esas bandas como EQ paramétrico (peaking RBJ,
//  Q=1.0) sobre la señal de altavoces. Es una PISTA psicoacústica,
//  no una interfaz neural, y así se documenta:
//
//    * Solo aumenta la PROBABILIDAD de percibir la dirección: los
//      resultados varían entre oyentes, y más aún por altavoces que
//      por audífonos (las HRTF individuales difieren).
//    * Solo funciona con señales de banda ancha que contengan altas
//      frecuencias; con fuentes de espectro estrecho o sin agudos,
//      el efecto se debilita o desaparece.
//    * Ganancias modestas (±3 dB máx.): no es un efecto, es un
//      sesgo espectral. Un realce mayor solo colorea el timbre.
//
//  API: presets above/front/behind/flat + amount 0..1 que escala las
//  ganancias. Bypass bit-exacto cuando todas las ganancias < 0.01 dB.
// =====================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ivanna::spatial {

// 5 bandas direccionales fijas (Hz), Q=1.0.
inline constexpr std::array<float, 5> kPinnaBandHz = {
    500.0f, 1000.0f, 4000.0f, 8000.0f, 10000.0f,
};
inline constexpr float kPinnaBandQ = 1.0f;
inline constexpr float kPinnaMaxGainDb = 3.0f;

enum class PinnaPreset : int {
    Flat = 0,    // sin sesgo: bypass bit-exacto
    Above = 1,   // arriba: +8 kHz, -4 kHz
    Front = 2,   // frente: +4 kHz, +500 Hz
    Behind = 3,  // atrás: +1 kHz, +10 kHz
};

// Ganancias (dB) por banda [500, 1000, 4000, 8000, 10000] a amount=1.
inline std::array<float, 5> pinnaPresetGains(PinnaPreset p) noexcept {
    switch (p) {
        case PinnaPreset::Above:
            return {0.0f, 0.0f, -2.0f, 3.0f, 0.0f};
        case PinnaPreset::Front:
            return {1.5f, 0.0f, 2.0f, 0.0f, 0.0f};
        case PinnaPreset::Behind:
            return {0.0f, 2.5f, 0.0f, 0.0f, 1.5f};
        case PinnaPreset::Flat:
        default:
            return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }
}

class PinnaElevationEq {
public:
    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        setPreset(PinnaPreset::Flat, 1.0f);
        reset();
    }

    // Solo limpia el ESTADO (x1/x2/y1/y2); los coeficientes que
    // calcula recompute() se conservan. (Antes: Biquad{} borraba
    // también los coeficientes y el preset quedaba en identidad.)
    void reset() noexcept {
        for (auto& b : bqL_) {
            b.x1 = 0.0f; b.x2 = 0.0f; b.y1 = 0.0f; b.y2 = 0.0f;
        }
        for (auto& b : bqR_) {
            b.x1 = 0.0f; b.x2 = 0.0f; b.y1 = 0.0f; b.y2 = 0.0f;
        }
    }

    // Hilo de control. amount 0..1 escala las ganancias del preset.
    void setPreset(PinnaPreset p, float amount = 1.0f) noexcept {
        const float a = std::clamp(amount, 0.0f, 1.0f);
        const auto g = pinnaPresetGains(p);
        for (std::size_t i = 0; i < gains_.size(); ++i) {
            gains_[i] = std::clamp(g[i] * a, -kPinnaMaxGainDb, kPinnaMaxGainDb);
        }
        recompute();
    }

    [[nodiscard]] std::array<float, 5> gainsDb() const noexcept {
        return gains_;
    }

    [[nodiscard]] bool isBypass() const noexcept {
        for (float g : gains_) {
            if (std::fabs(g) >= 0.01f) return false;
        }
        return true;
    }

    // Magnitud de la cascada (dB) a una frecuencia, para verificación
    // y para UI. 0 dB cuando el preset es plano.
    [[nodiscard]] float magnitudeDbAt(float freqHz) const noexcept {
        if (!std::isfinite(freqHz) || freqHz <= 0.0f) return 0.0f;
        const float w = 2.0f * 3.141592653589793f * freqHz / sampleRate_;
        const float cw = std::cos(w);
        const float sw = std::sin(w);
        float mag2 = 1.0f;
        for (std::size_t i = 0; i < gains_.size(); ++i) {
            const Biquad& q = bqL_[i];
            // |H(e^jw)|^2 con coeficientes normalizados (a0=1).
            const float re = q.b0 + q.b1 * cw + q.b2 * (2.0f * cw * cw - 1.0f);
            const float im = -q.b1 * sw - q.b2 * (2.0f * sw * cw);
            const float reA = 1.0f + q.a1 * cw + q.a2 * (2.0f * cw * cw - 1.0f);
            const float imA = -q.a1 * sw - q.a2 * (2.0f * sw * cw);
            const float num = re * re + im * im;
            const float den = reA * reA + imA * imA;
            mag2 *= (den > 1e-12f) ? (num / den) : 1.0f;
        }
        return 10.0f * std::log10(std::max(mag2, 1e-12f));
    }

    // Hilo de audio. Bypass bit-exacto si el preset es plano.
    void process(float* left, float* right, int n) noexcept {
        if (!left || !right || n <= 0 || !prepared_) return;
        if (isBypass()) return;
        for (int i = 0; i < n; ++i) {
            float xL = left[i], xR = right[i];
            for (std::size_t b = 0; b < gains_.size(); ++b) {
                xL = runBiquad(bqL_[b], xL);
                xR = runBiquad(bqR_[b], xR);
            }
            left[i] = xL;
            right[i] = xR;
        }
    }

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    static float runBiquad(Biquad& q, float x) noexcept {
        const float y = q.b0 * x + q.b1 * q.x1 + q.b2 * q.x2 - q.a1 * q.y1 -
                        q.a2 * q.y2;
        q.x2 = q.x1;
        q.x1 = x;
        q.y2 = q.y1;
        if (!std::isfinite(y)) {
            q.y1 = 0.0f;
            q.x1 = 0.0f;
            q.x2 = 0.0f;
            q.y2 = 0.0f;
            return x;
        }
        q.y1 = y;
        return y;
    }

    void recompute() noexcept {
        // Peaking RBJ por banda, coeficientes normalizados (a0=1).
        const float pi = 3.141592653589793f;
        for (std::size_t i = 0; i < gains_.size(); ++i) {
            const float fc = kPinnaBandHz[i];
            const float g = gains_[i];
            const float a = std::pow(10.0f, g / 40.0f);
            const float w0 = 2.0f * pi * fc / sampleRate_;
            const float alpha = std::sin(w0) / (2.0f * kPinnaBandQ);
            const float cw = std::cos(w0);
            const float a0 = 1.0f + alpha / a;
            Biquad q{};
            q.b0 = (1.0f + alpha * a) / a0;
            q.b1 = (-2.0f * cw) / a0;
            q.b2 = (1.0f - alpha * a) / a0;
            q.a1 = (-2.0f * cw) / a0;
            q.a2 = (1.0f - alpha / a) / a0;
            bqL_[i] = q;
            bqR_[i] = q;
        }
        prepared_ = true;
    }

    float sampleRate_ = 48000.0f;
    bool prepared_ = false;
    std::array<float, 5> gains_{};
    std::array<Biquad, 5> bqL_{};
    std::array<Biquad, 5> bqR_{};
};

}  // namespace ivanna::spatial
