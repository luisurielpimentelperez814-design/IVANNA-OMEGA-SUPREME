// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================================
 * IVANNA OMEGA SUPREME — CrosstalkCanceller (immersion/)
 * ============================================================================
 *
 * El WFS, el HOA y la HRTF de IVANNA producen una señal BINAURAL: pensada para
 * audífonos. Por AUX hacia un minicomponente (Sony Genezi) cada altavoz también
 * llega al oído contrario y el campo binaural se colapsa hacia el centro.
 * Este módulo cancela esa diafonía (Schroeder–Atal recursivo) para que el campo
 * se sostenga en altavoces.
 *
 * Estructura (por canal, recursiva, retardo >= 1 muestra => estable si g < 1):
 *
 *   yL[n] = xL[n] − g · H( yR[n − N] )
 *   yR[n] = xR[n] − g · H( yL[n − N] )
 *
 *   N = round( fs · (a/c) · (θ + sin θ) )   (ITD de Woodworth del ángulo θ de
 *                                            cada altavoz respecto al eje)
 *   H = pasa-banda 250 Hz – 6 kHz (sombra de cabeza): limita el realce de
 *       graves de la inversión y evita cancelar donde la cabeza ya no hace
 *       sombra coherente.
 *
 * Garantías:
 *   - amount = 0 => salida bit-exacta a la entrada (bypass real).
 *   - |g·H| < 1 siempre (g <= kMaxG = 0.6): sin inestabilidad posible.
 *   - Sin malloc, sin excepciones, anillos de tamaño fijo; amount suavizado
 *     por muestra (~10 ms) para que activar/desactivar no haga click.
 *
 * Límites honestos: el modelo asume un oyente en el eje de simetría (sweet
 * spot) y altavoces simétricos; fuera de ahí la cancelación se degrada.
 * Contenido en antifase dentro de banda se realza hasta 1/(1−g) (≈ +8 dB con
 * g = 0.6): el SafetyLimiter de salida sigue siendo la última barrera.
 * ============================================================================
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ivanna::immersion {

/** Sombra de cabeza: pasa-banda de dos polos simples (HP de 250 Hz, LP de 6 kHz). */
struct ShadowFilter {
    float aLp = 0.0f, aHp = 0.0f, lp = 0.0f, hp = 0.0f;

    void prepare(float sampleRate, float fHpHz = 250.0f, float fLpHz = 6000.0f) noexcept {
        const float nyq = 0.45f * sampleRate;
        const float fl = std::min(fLpHz, nyq);
        aLp = 1.0f - std::exp(-2.0f * 3.14159265358979f * fl / sampleRate);
        aHp = 1.0f - std::exp(-2.0f * 3.14159265358979f * std::min(fHpHz, fl) / sampleRate);
        reset();
    }
    void reset() noexcept { lp = hp = 0.0f; }
    inline float process(float x) noexcept {
        lp += aLp * (x - lp);
        hp += aHp * (lp - hp);
        if (std::fabs(lp) < 1e-20f) lp = 0.0f;   // sin subnormales en silencio
        if (std::fabs(hp) < 1e-20f) hp = 0.0f;
        return lp - hp;
    }
};

class CrosstalkCanceller {
public:
    static constexpr int   kRing  = 64;     // potencia de 2; N máx = 63
    static constexpr float kMaxG  = 0.6f;   // |g| < 1 garantiza estabilidad
    static constexpr float kSoundSpeed = 343.0f;

    /**
     * @param halfAngleDeg  ángulo de cada altavoz respecto al eje (30° = clásico ±30°).
     * @param headRadiusM   radio de cabeza (0.0875 m promedio).
     */
    void prepare(float sampleRate, float halfAngleDeg = 30.0f, float headRadiusM = 0.0875f) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        const float deg = std::isfinite(halfAngleDeg) ? std::clamp(halfAngleDeg, 5.0f, 60.0f) : 30.0f;
        const float a = std::isfinite(headRadiusM) ? std::clamp(headRadiusM, 0.06f, 0.12f) : 0.0875f;
        const float th = deg * 3.14159265358979f / 180.0f;
        const float itd = (a / kSoundSpeed) * (th + std::sin(th));
        delay_ = std::clamp(static_cast<int>(std::lround(itd * sr_)), 1, kRing - 1);
        smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * sr_));
        shadowL_.prepare(sr_);
        shadowR_.prepare(sr_);
        reset();
    }

    void reset() noexcept {
        ringL_.fill(0.0f);
        ringR_.fill(0.0f);
        pos_ = 0;
        shadowL_.reset();
        shadowR_.reset();
        amountSmooth_ = amountTarget_;
    }

    /** 0 = bypass bit-exacto, 1 = cancelación completa (g = kMaxG). */
    void setAmount(float amount01) noexcept {
        amountTarget_ = std::isfinite(amount01) ? std::clamp(amount01, 0.0f, 1.0f) : 0.0f;
    }
    float amount() const noexcept { return amountTarget_; }
    int delaySamples() const noexcept { return delay_; }

    /** Realce máximo en banda (dB) para un g dado: 20·log10(1/(1−g)). */
    static float worstCaseBoostDb(float g) noexcept {
        const float gg = std::clamp(g, 0.0f, kMaxG);
        return -20.0f * std::log10(1.0f - gg);
    }

    /** In-place. l y r deben tener n muestras; punteros nulos => no hace nada. */
    void process(float* l, float* r, std::size_t n) noexcept {
        if (l == nullptr || r == nullptr) return;
        for (std::size_t i = 0; i < n; ++i) {
            amountSmooth_ += (amountTarget_ - amountSmooth_) * smoothCoeff_;
            const bool settledOff = (amountTarget_ <= 0.0f) && (amountSmooth_ < 1e-6f);
            const float xl = std::isfinite(l[i]) ? l[i] : 0.0f;
            const float xr = std::isfinite(r[i]) ? r[i] : 0.0f;

            float yl, yr;
            if (settledOff) {
                amountSmooth_ = 0.0f;
                yl = xl;
                yr = xr;
            } else {
                const float g = amountSmooth_ * kMaxG;
                const int rd = (pos_ - delay_) & (kRing - 1);
                const float fbR = shadowR_.process(ringR_[rd]);
                const float fbL = shadowL_.process(ringL_[rd]);
                yl = xl - g * fbR;
                yr = xr - g * fbL;
            }
            ringL_[pos_] = yl;
            ringR_[pos_] = yr;
            pos_ = (pos_ + 1) & (kRing - 1);
            l[i] = yl;
            r[i] = yr;
        }
    }

private:
    float sr_ = 48000.0f;
    int   delay_ = 12;
    float smoothCoeff_ = 0.002f;
    float amountTarget_ = 0.0f, amountSmooth_ = 0.0f;
    std::array<float, kRing> ringL_{}, ringR_{};
    int   pos_ = 0;
    ShadowFilter shadowL_{}, shadowR_{};
};

}  // namespace ivanna::immersion
