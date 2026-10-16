// AuxFieldCoherence.hpp
// ============================================================================
// IVANNA — Campo de presión coherente para salida por ALTAVOCES / AUX
// ============================================================================
// El WFS/HRTF del motor renderiza binauralmente (pensado para audífonos). Por
// AUX hacia un amplificador con woofers, ese renderizado no aplica: lo que
// importa es que los graves y sus transitorios lleguen EN FASE y con la misma
// presión desde ambos lados, para que la sala se excite como un campo de
// presión coherente y no como "dos bocinas".
//
// Técnica (estándar, verificable): crossover Linkwitz-Riley de 4º orden.
//   LP(f) + HP(f) suma plana en magnitud y alineada en fase (all-pass), así que
//   la banda alta queda INTACTA (estéreo, imagen y transitorios agudos) y solo
//   la banda baja se trata:
//       baja'_c = (1-k)·baja_c + k·0.5·(baja_L + baja_R)        k = coherencia 0..1
//   Con k=1 los graves son idénticos en ambos canales (presión uniforme, sin
//   cancelaciones de fase entre woofers); con k=0 es solo el all-pass LR4.
//
// Lo que NO hace (a propósito): no mide la sala ni compensa su respuesta — eso
// es corrección acústica (RIR/SAF/AutoEQ), otro módulo.
//
// RT-safe: cero asignaciones, cero locks, estado fijo. Activación sin clic:
// fundido de 20 ms entre la señal original y la tratada; con el modo apagado y
// el fundido en 0 el audio pasa BIT-EXACTO (no se toca ni una muestra).
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ivanna::dsp {

class AuxFieldCoherence {
public:
    static constexpr float kMinXoverHz = 40.0f;
    static constexpr float kMaxXoverHz = 250.0f;

    void prepare(float sampleRate) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        fadeStep_ = 1.0f / (0.020f * sr_);
        coefXover_ = -1.0f;            // fuerza recálculo
        reset();
        updateCoefs(xoverHz_);
    }

    /// Hilo de audio, al inicio de cada bloque (valores ya validados aquí).
    void setParams(bool enabled, float xoverHz, float coherence) noexcept {
        enabled_ = enabled;
        if (std::isfinite(xoverHz)) xoverHz_ = std::clamp(xoverHz, kMinXoverHz, kMaxXoverHz);
        if (std::isfinite(coherence)) coh_ = std::clamp(coherence, 0.0f, 1.0f);
    }

    bool isActive() const noexcept { return enabled_ || fade_ > 0.0f; }
    float fade() const noexcept { return fade_; }

    void reset() noexcept {
        for (auto& s : st_) s = Bq{};
        fade_ = 0.0f;
    }

    void process(float* L, float* R, std::size_t n) noexcept {
        if (!L || !R || n == 0) return;
        if (!enabled_ && fade_ <= 0.0f) return;            // bypass bit-exacto
        if (std::fabs(xoverHz_ - coefXover_) > 0.25f) updateCoefs(xoverHz_);
        const float target = enabled_ ? 1.0f : 0.0f;
        const float k = coh_;
        for (std::size_t i = 0; i < n; ++i) {
            const float dl = L[i], dr = R[i];
            const float lowL  = run(st_[0], run(st_[1], dl, lp_), lp_);
            const float lowR  = run(st_[2], run(st_[3], dr, lp_), lp_);
            const float highL = run(st_[4], run(st_[5], dl, hp_), hp_);
            const float highR = run(st_[6], run(st_[7], dr, hp_), hp_);
            const float mono = 0.5f * (lowL + lowR);
            const float pl = highL + (1.0f - k) * lowL + k * mono;
            const float pr = highR + (1.0f - k) * lowR + k * mono;
            if (fade_ < target)      fade_ = std::min(target, fade_ + fadeStep_);
            else if (fade_ > target) fade_ = std::max(target, fade_ - fadeStep_);
            const float f = fade_;
            L[i] = dl + f * (pl - dl);
            R[i] = dr + f * (pr - dr);
        }
        if (!enabled_ && fade_ <= 0.0f) reset();            // estado limpio para la próxima activación
    }

private:
    struct Coef { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Bq { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    static float run(Bq& s, float x, const Coef& c) noexcept {
        const float y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }

    void updateCoefs(float fc) noexcept {
        coefXover_ = fc;
        // Butterworth 2º orden (Q = 1/√2) en cascada x2 = Linkwitz-Riley 4º orden.
        const float w0 = 6.28318530718f * fc / sr_;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        const float inv = 1.0f / a0;
        lp_.b0 = (1.0f - cw) * 0.5f * inv; lp_.b1 = (1.0f - cw) * inv; lp_.b2 = lp_.b0;
        hp_.b0 = (1.0f + cw) * 0.5f * inv; hp_.b1 = -(1.0f + cw) * inv; hp_.b2 = hp_.b0;
        lp_.a1 = hp_.a1 = -2.0f * cw * inv;
        lp_.a2 = hp_.a2 = (1.0f - alpha) * inv;
    }

    float sr_ = 48000.0f, fadeStep_ = 1.0f / (0.020f * 48000.0f);
    bool  enabled_ = false;
    float xoverHz_ = 120.0f, coh_ = 1.0f, coefXover_ = -1.0f, fade_ = 0.0f;
    Coef lp_, hp_;
    Bq st_[8];
};

} // namespace ivanna::dsp
