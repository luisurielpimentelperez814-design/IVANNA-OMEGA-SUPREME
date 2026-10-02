// LateReverbSuppressor.hpp — Supresor Estadístico de Cola Difusa (Lebart & Habets 2001)
// con Partición Complementaria Exacta de 3er Orden (18 dB/oct @ 250 Hz).
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Especificación Matemática M5 / Sección 4.2 / Prueba T4:
//   - Estima la energía reverberante tardía eLate(n) a partir de la señal retardada
//     Delta = 50 ms frente a la envolvente rápida directa eFast(n) (tau = 8 ms).
//   - Razón de cola difusa: rho_late = eLate / (eFast + eLate + eps) en [0, 1].
//   - Ganancia espectral suave: g_target = max(gMin, 1 - strength * rho_late).
//   - Partición complementaria exacta de 3 polos a 250 Hz: sub = x - hp3(x),
//     garantizando reconstrucción bit-a-bit cuando g == 1.0 y variación < 0.08 dB a 60 Hz.
//   - 100% RT-safe: buffers estáticos, cero asignaciones dinámicas, cero bloqueos.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace ivanna { namespace spatial {

class LateReverbSuppressor {
public:
    static constexpr int kMaxTap = 8192; // Soporta 50 ms hasta 96 kHz (4800 muestras)

    void prepare(float sr) noexcept {
        fs_    = (sr > 8000.0f) ? sr : 48000.0f;
        tap_   = std::clamp(static_cast<int>(0.050f * fs_), 256, kMaxTap - 1);
        aFast_ = 1.0f - std::exp(-1.0f / (0.008f * fs_));
        aSlow_ = 1.0f - std::exp(-1.0f / (0.120f * fs_));
        aGain_ = 1.0f - std::exp(-1.0f / (0.015f * fs_));
        aMid_  = 1.0f - std::exp(-6.283185307179586f * 250.0f / fs_);
        reset();
    }

    void reset() noexcept {
        std::memset(dl_, 0, sizeof(dl_));
        std::memset(dr_, 0, sizeof(dr_));
        idx_       = 0;
        eFast_     = 0.0f;
        eLate_     = 0.0f;
        g_         = 1.0f;
        lateRatio_ = 0.0f;
        lpL1_ = lpL2_ = lpL3_ = 0.0f;
        lpR1_ = lpR2_ = lpR3_ = 0.0f;
    }

    // strength en [0, 1] (escalado por envDepth del Atlas y guarda M10)
    void process(float* __restrict L,
                 float* __restrict R,
                 size_t n,
                 float strength) noexcept {
        if (!L || !R || n == 0) return;
        strength = std::clamp(strength, 0.0f, 0.85f);
        const float gMin = 1.0f - 0.65f * strength;

        for (size_t i = 0; i < n; ++i) {
            const float inL = std::isfinite(L[i]) ? L[i] : 0.0f;
            const float inR = std::isfinite(R[i]) ? R[i] : 0.0f;

            // Cascada de 3 polos a 250 Hz para extraer hp3 (> 250 Hz, 18 dB/oct).
            // sub = in - hp3 garantiza reconstrucción algebraica exacta cuando g_ == 1.0
            // y rechazo > 37 dB de la componente residual a 60 Hz (< 0.08 dB de variación).
            lpL1_ += aMid_ * (inL - lpL1_);
            const float hpL1 = inL - lpL1_;
            lpL2_ += aMid_ * (hpL1 - lpL2_);
            const float hpL2 = hpL1 - lpL2_;
            lpL3_ += aMid_ * (hpL2 - lpL3_);
            const float hpL3 = hpL2 - lpL3_;
            const float subL = inL - hpL3;

            lpR1_ += aMid_ * (inR - lpR1_);
            const float hpR1 = inR - lpR1_;
            lpR2_ += aMid_ * (hpR1 - lpR2_);
            const float hpR2 = hpR1 - lpR2_;
            lpR3_ += aMid_ * (hpR2 - lpR3_);
            const float hpR3 = hpR2 - lpR3_;
            const float subR = inR - hpR3;

            // Señal retardada Delta = 50 ms sobre la banda reverberante (> 250 Hz)
            const float dL = dl_[idx_];
            const float dR = dr_[idx_];
            dl_[idx_] = hpL3;
            dr_[idx_] = hpR3;
            if (++idx_ >= tap_) idx_ = 0;

            // Energía instantánea actual (directa + temprana) vs retardada 50 ms (cola difusa)
            // Ponderamos energía estéreo y componente Side para máxima sensibilidad a campo difuso
            const float sSide = 0.5f * (hpL3 - hpR3);
            const float dSide = 0.5f * (dL - dR);
            const float s2 = 0.5f * (hpL3 * hpL3 + hpR3 * hpR3) + 0.5f * (sSide * sSide);
            const float d2 = 0.5f * (dL * dL + dR * dR)         + 0.5f * (dSide * dSide);

            eFast_ += aFast_ * (s2 - eFast_);
            // Ataque rápido cuando llega la cola a los 50 ms, decaimiento estadístico lento (120 ms)
            const float aLate = (d2 > eLate_) ? aFast_ : aSlow_;
            eLate_ += aLate * (d2 - eLate_);

            float ratio = 0.0f;
            if (eLate_ > 1.0e-12f) {
                ratio = std::clamp(eLate_ / (eFast_ + eLate_ + 1.0e-12f), 0.0f, 1.0f);
            }
            lateRatio_ = ratio;

            const float gT = (eLate_ > 1.0e-12f)
                           ? std::max(gMin, 1.0f - strength * ratio)
                           : 1.0f;
            g_ += aGain_ * (gT - g_);
            g_ = std::clamp(g_, gMin, 1.0f);

            L[i] = subL + g_ * hpL3;
            R[i] = subR + g_ * hpR3;
        }
    }

    float lateRatio() const noexcept { return lateRatio_; }
    float currentGain() const noexcept { return g_; }
    int   tapSamples() const noexcept { return tap_; }

private:
    float fs_        = 48000.0f;
    int   tap_       = 2400;
    int   idx_       = 0;
    float aFast_     = 0.0026f;
    float aSlow_     = 0.00017f;
    float aGain_     = 0.0014f;
    float aMid_      = 0.032f;
    float eFast_     = 0.0f;
    float eLate_     = 0.0f;
    float g_         = 1.0f;
    float lateRatio_ = 0.0f;

    float lpL1_ = 0.0f, lpL2_ = 0.0f, lpL3_ = 0.0f;
    float lpR1_ = 0.0f, lpR2_ = 0.0f, lpR3_ = 0.0f;
    float dl_[kMaxTap]{};
    float dr_[kMaxTap]{};
};

}} // namespace ivanna::spatial
