// RoomCorrectionEq.hpp
// ============================================================================
// IVANNA — Etapa que APLICA la corrección acústica de sala medida por RoomProbe.
// ============================================================================
// Reutiliza AutoEqFilter (cascada de 10 biquads peaking RBJ que ya existía en el proyecto y solo vivía dentro del
// renderer de objetos) con las frecuencias de RoomProbe (31.5 Hz … 16 kHz, Q ≈ 1.41, una octava).
//
//   · Apagada (todas las ganancias en 0) → BIT-EXACTO: no se toca una muestra ni se ejecuta un biquad.
//   · Los cambios de ganancia se deslizan a 0.25 dB por bloque: aplicar o quitar la corrección no hace clic.
//   · Las ganancias objetivo llegan ya acotadas por RoomProbe (−9 … +3 dB); aquí se acota de nuevo a ±12 dB.
// RT-safe: sin asignaciones ni locks (los objetivos se leen del hilo de audio como atómicos en el motor).
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include "auto_eq_filter.hpp"
#include "RoomProbe.hpp"

namespace ivanna::dsp {

class RoomCorrectionEq {
public:
    static constexpr float kSlewDbPerBlock = 0.25f;

    void prepare(float sampleRate) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        eq_.init(sr_);
        for (int b = 0; b < room::kBands; ++b) { cur_[b] = 0.0f; tgt_[b] = 0.0f; }
        active_ = false;
        safetyGain_ = 1.0f;
        eq_.setEnabled(false);
    }

    /// Objetivos en dB para las 10 bandas de RoomProbe (hilo de audio, al inicio del bloque).
    void setTarget(const float* gDb) noexcept {
        if (gDb == nullptr) return;
        for (int b = 0; b < room::kBands; ++b)
            tgt_[b] = std::isfinite(gDb[b]) ? std::clamp(gDb[b], -12.0f, 12.0f) : 0.0f;
    }

    bool  isActive() const noexcept { return active_; }
    float currentDb(int b) const noexcept { return (b >= 0 && b < room::kBands) ? cur_[b] : 0.0f; }

    void process(float* L, float* R, int n) noexcept {
        if (L == nullptr || R == nullptr || n <= 0) return;
        bool changed = false, any = false;
        for (int b = 0; b < room::kBands; ++b) {
            const float d = tgt_[b] - cur_[b];
            if (std::fabs(d) > 1.0e-4f) {
                cur_[b] += std::clamp(d, -kSlewDbPerBlock, kSlewDbPerBlock);
                changed = true;
            }
            if (std::fabs(cur_[b]) > 1.0e-3f) any = true;
        }
        if (changed && any) {
            if (!active_) eq_.init(sr_);                     // activando: estados limpios
            for (int b = 0; b < room::kBands; ++b) eq_.setBand(b, room::kBandHz[b], cur_[b], room::kBandQ);
        }
        if (any != active_) { active_ = any; eq_.setEnabled(any); }
        if (!active_) return;                                // bypass bit-exacto

        // Sanitize inputs before biquad state is touched, then enforce a stereo-linked
        // post-EQ ceiling. The correction path must never undo downstream headroom.
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(L[i])) L[i] = 0.0f;
            if (!std::isfinite(R[i])) R[i] = 0.0f;
        }
        eq_.process(L, R, n);

        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(L[i])) L[i] = 0.0f;
            if (!std::isfinite(R[i])) R[i] = 0.0f;
            peak = std::max(peak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
        const float requiredGain = peak > 0.98f ? 0.98f / peak : 1.0f;
        if (requiredGain < safetyGain_) safetyGain_ = requiredGain; // instant attack
        else safetyGain_ = std::min(requiredGain, safetyGain_ + 0.05f);    // bounded release
        for (int i = 0; i < n; ++i) {
            const float left = L[i] * safetyGain_;
            const float right = R[i] * safetyGain_;
            L[i] = std::isfinite(left) ? std::clamp(left, -0.98f, 0.98f) : 0.0f;
            R[i] = std::isfinite(right) ? std::clamp(right, -0.98f, 0.98f) : 0.0f;
        }
    }

private:
    ivanna::spatial::AutoEqFilter eq_;
    float sr_ = 48000.0f;
    float cur_[room::kBands] = {};
    float tgt_[room::kBands] = {};
    float safetyGain_ = 1.0f;
    bool  active_ = false;
};

}  // namespace ivanna::dsp
