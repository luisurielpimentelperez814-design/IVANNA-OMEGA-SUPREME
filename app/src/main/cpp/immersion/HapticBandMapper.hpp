// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================================
 * IVANNA OMEGA SUPREME — HapticBandMapper (immersion/)
 * ============================================================================
 *
 * Convierte audio ya procesado en eventos hápticos por BANDAS, para ampliar la
 * háptica de graves existente (BassHaptics.kt) más allá del bombo:
 *
 *   impact  : golpe de sub/bajo (20–120 Hz) por salto de envolvente rápida
 *             contra lenta → pulso corto y fuerte.
 *   rumble  : energía sostenida de sub → capa continua, suave.
 *   texture : transitorios de alta frecuencia (>2.5 kHz) → micro-pulsos leves
 *             (hi-hats, chasquidos) que dan "textura" al cuerpo.
 *
 * Salida por bloque: HapticFrame con intensidades 0..1 y una conversión a
 * amplitud 1..255 + duración en ms, el formato que consume VibrationEffect en
 * Android (la capa Kotlin decide el motor/waveform; aquí no hay JNI).
 *
 * Protección del motor (límites honestos de un vibrador de teléfono):
 *   - Intensidad MEDIA máxima 60 % sobre una ventana móvil de ~1 s (mismo tope
 *     que el retumbo actual): cuando la ventana se agota, el límite escala la
 *     salida de los tres canales a la vez hasta recuperar margen.
 *   - Separación mínima de 25 ms entre pulsos de impacto.
 *   - Sin malloc ni hilos; entradas no finitas se ignoran.
 *
 * Estado: módulo nuevo probado en host; lo consume ImmersionCore. La capa
 * Kotlin existente no cambia en este commit.
 * ============================================================================
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ivanna::immersion {

struct HapticFrame {
    float impact  = 0.0f;   // 0..1
    float rumble  = 0.0f;   // 0..1
    float texture = 0.0f;   // 0..1
    uint8_t amplitude = 0;  // 0..255 (0 = sin pulso), ya con límite de duty
    uint16_t durationMs = 0;
};

class HapticBandMapper {
public:
    static constexpr float kMaxDuty = 0.60f;

    void prepare(float sampleRate, int blockFrames) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        block_ = std::clamp(blockFrames, 16, 8192);
        blockSec_ = static_cast<float>(block_) / sr_;
        dutyBlocks_ = std::clamp(static_cast<int>(std::lround(1.0f / blockSec_)), 4, kMaxDutyRing);
        aSub_ = coeff(120.0f);
        aSubHp_ = coeff(20.0f);
        aHi_ = coeff(2500.0f);
        aFast_ = 1.0f - std::exp(-1.0f / (0.004f * sr_));
        relFast_ = std::exp(-1.0f / (0.030f * sr_));
        aSlow_ = 1.0f - std::exp(-1.0f / (0.200f * sr_));
        aRumble_ = 1.0f - std::exp(-blockSec_ / 0.080f);
        aTex_ = 1.0f - std::exp(-1.0f / (0.002f * sr_));
        reset();
    }

    void reset() noexcept {
        lp1_ = lp2_ = hpState_ = hiLp_ = 0.0f;
        envFast_ = envSlow_ = texFast_ = texSlow_ = 0.0f;
        rumble_ = 0.0f;
        sinceImpactSec_ = 1.0f;
        armed_ = true;
        duty_.fill(0.0f);
        dutyPos_ = 0;
        dutySum_ = 0.0f;
        last_ = HapticFrame{};
    }

    /** Ganancia global 0..1 (la fija el usuario o el ArousalConductor). */
    void setGain(float g) noexcept { gain_ = std::isfinite(g) ? std::clamp(g, 0.0f, 1.0f) : 0.0f; }

    /** Procesa un bloque mono (suma L+R ya hecha por el llamador). */
    HapticFrame process(const float* x, std::size_t n) noexcept {
        HapticFrame f;
        if (x == nullptr || n == 0) { last_ = f; pushDuty(0.0f); return f; }

        float subPeakRatio = 0.0f, texPeakRatio = 0.0f, subMs = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            const float s = std::isfinite(x[i]) ? x[i] : 0.0f;
            // Banda sub: LPF 120 Hz (dos polos) y HPF 20 Hz (quita DC/infrasonido de ruido).
            lp1_ += aSub_ * (s - lp1_);
            lp2_ += aSub_ * (lp1_ - lp2_);
            hpState_ += aSubHp_ * (lp2_ - hpState_);
            const float sub = lp2_ - hpState_;
            const float a = std::fabs(sub);
            envFast_ = (a > envFast_) ? envFast_ + aFast_ * (a - envFast_) : envFast_ * relFast_;
            envSlow_ += aSlow_ * (a - envSlow_);
            subMs += sub * sub;
            const float r = envFast_ / std::max(envSlow_, 1e-4f);
            if (envFast_ > 0.02f) subPeakRatio = std::max(subPeakRatio, r);

            // Banda alta: HPF por resta del LPF de 2.5 kHz.
            hiLp_ += aHi_ * (s - hiLp_);
            const float hi = std::fabs(s - hiLp_);
            texFast_ = (hi > texFast_) ? texFast_ + aTex_ * (hi - texFast_) : texFast_ * relFast_;
            texSlow_ += aSlow_ * (hi - texSlow_);
            const float tr = texFast_ / std::max(texSlow_, 1e-4f);
            if (texFast_ > 0.02f) texPeakRatio = std::max(texPeakRatio, tr);
        }
        sinceImpactSec_ += blockSec_;

        // Rumble: RMS de sub sostenido, suavizado ~80 ms.
        const float subRms = std::sqrt(subMs / static_cast<float>(n));
        rumble_ += aRumble_ * (clamp01(subRms * 4.0f) - rumble_);

        // Rearme por histéresis: un golpe que aún suena no vuelve a disparar;
        // hay que bajar de 1.6× la envolvente lenta antes del siguiente.
        const float ratioEnd = envFast_ / std::max(envSlow_, 1e-4f);
        if (!armed_ && (ratioEnd < 1.6f || envFast_ < 0.01f)) armed_ = true;
        float impact = 0.0f;
        if (armed_ && subPeakRatio > 2.2f && sinceImpactSec_ >= 0.025f) {
            impact = clamp01((subPeakRatio - 2.2f) / 3.0f + 0.35f);
            sinceImpactSec_ = 0.0f;
            armed_ = false;
        }
        const float texture = (texPeakRatio > 3.0f) ? clamp01((texPeakRatio - 3.0f) / 5.0f) * 0.5f : 0.0f;

        // Límite de intensidad media: la "carga" del bloque es su pico activo (0..1).
        const float desiredPeak = std::max({impact, 0.6f * rumble_, texture}) * gain_;
        const float allowed = std::max(0.0f, kMaxDuty * static_cast<float>(dutyBlocks_) - dutySum_);
        const float outPeak = std::min(desiredPeak, allowed);
        const float scale = (desiredPeak > 1e-6f) ? outPeak / desiredPeak : 0.0f;
        f.impact  = impact * gain_ * scale;
        f.rumble  = rumble_ * gain_ * scale;
        f.texture = texture * gain_ * scale;
        pushDuty(outPeak);
        if (outPeak > 0.05f) {
            f.amplitude = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(outPeak * 255.0f)), 1, 255));
            const bool textureLeads = f.texture > std::max(f.impact, 0.6f * f.rumble);
            f.durationMs = textureLeads ? 12 : 40;
        }
        last_ = f;
        return f;
    }

    const HapticFrame& last() const noexcept { return last_; }
    /** Intensidad media (0..1) en la ventana móvil de ~1 s. */
    float dutyFraction() const noexcept { return dutySum_ / static_cast<float>(dutyBlocks_); }

private:
    static constexpr int kMaxDutyRing = 512;

    static float clamp01(float v) noexcept { return std::min(1.0f, std::max(0.0f, v)); }
    float coeff(float hz) const noexcept { return 1.0f - std::exp(-2.0f * 3.14159265358979f * hz / sr_); }

    void pushDuty(float active) noexcept {
        dutySum_ += active - duty_[dutyPos_];
        duty_[dutyPos_] = active;
        dutyPos_ = (dutyPos_ + 1) % dutyBlocks_;
        if (dutySum_ < 0.0f) dutySum_ = 0.0f;
    }

    float sr_ = 48000.0f, blockSec_ = 0.008f;
    int block_ = 384, dutyBlocks_ = 100;
    float aSub_ = 0, aSubHp_ = 0, aHi_ = 0, aFast_ = 0, relFast_ = 0, aSlow_ = 0, aRumble_ = 0, aTex_ = 0;
    float lp1_ = 0, lp2_ = 0, hpState_ = 0, hiLp_ = 0;
    float envFast_ = 0, envSlow_ = 0, texFast_ = 0, texSlow_ = 0, rumble_ = 0;
    float sinceImpactSec_ = 1.0f, gain_ = 1.0f;
    bool  armed_ = true;
    std::array<float, kMaxDutyRing> duty_{};
    int dutyPos_ = 0;
    float dutySum_ = 0.0f;
    HapticFrame last_{};
};

}  // namespace ivanna::immersion
