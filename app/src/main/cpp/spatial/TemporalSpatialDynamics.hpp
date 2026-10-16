// TemporalSpatialDynamics.hpp
// ============================================================================
// IVANNA — Dinámica espacial en el TIEMPO ("4D": la imagen respira con la música)
// ============================================================================
// Las tres dimensiones espaciales ya las cubre el motor (HRTF/HOA/WFS, ancho, profundidad). Lo que ninguna etapa
// hacía era mover el espacio EN EL TIEMPO ligado a la música: el ancho era un valor fijo. Esta etapa lo hace con
// una técnica clásica y verificable:
//
//   · En cada TRANSITORIO (golpe, ataque de caja, pluck) las partes laterales se recogen hacia el centro
//     (la imagen se "enfoca" → el impacto suena más compacto y con más pegada).
//   · En lo SOSTENIDO (colas, cuerdas, ambiente) las partes laterales se abren ("bloom" → más envolvente).
//
// El detector es el TransientDetector que ya existía en el proyecto (comparación de dos envolventes, sin IA);
// aquí solo se reutiliza. El gain lateral se suaviza: ataque 3 ms, relajación ~140 ms, con 25 ms de retención
// tras un golpe para que no tiemble.
//
// LEY ADAPTATIVA (modo auto, opcional): la profundidad efectiva escala con la DENSIDAD de golpes medida en la
// propia música, no con un valor fijo:
//     r  = golpes por segundo (flancos de subida del detector, media móvil de 2 s)
//     s  = 1 - exp(-r / 3)                       (0 sin golpes → ~0.63 a 3/s → ~0.86 a 6/s)
//     a_ef = a_usuario · (0.4 + 0.6·s)           (el slider es el TECHO; nunca se supera)
// Música densa en ataques (electrónica, rock) recibe todo el enfoque; ambiente/cuerdas sostenidas, un 40 % (el
// bloom sigue ahí pero sin pegar cada nota). Es una heurística de diseño con coeficientes elegidos a mano: acotada y
// probada, NO validada con escucha — por eso se puede apagar y el slider manda.
//
// Propiedades garantizadas (y probadas en tests/test_temporal_spatial.cpp):
//   · El canal CENTRAL (M = (L+R)/2) no se toca nunca: L'+R' == L+R. Compatible en mono y no cambia el centro.
//   · Solo se modifica el lateral POR ENCIMA de ~250 Hz (dos pasa-altos de 1er orden): los graves, que el modo AUX
//     deja coherentes, no se vuelven a abrir.
//   · Apagado y con el fundido en 0 el audio pasa BIT-EXACTO. Activación sin clic (fundido de 20 ms).
//   · RT-safe: sin asignaciones ni locks.
//
// Lo que NO es: no es espacialización binaural ni mide la sala. Es modulación temporal del ancho lateral.
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "TransientDetector.hpp"

namespace ivanna::dsp {

class TemporalSpatialDynamics {
public:
    static constexpr std::size_t kMaxChunk = 1024;
    static constexpr float kTightPerAmount = 0.60f;  // a amount=1, el lateral cae a 40 % en un golpe
    static constexpr float kBloomPerAmount = 0.35f;  // a amount=1, el lateral sube a 135 % en lo sostenido
    static constexpr float kSideCornerHz   = 250.0f;

    void prepare(float sampleRate) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        detector_.prepare(sr_);
        atk_      = 1.0f - std::exp(-1.0f / (0.003f * sr_));
        rel_      = 1.0f - std::exp(-1.0f / (0.140f * sr_));
        holdLen_  = static_cast<int>(0.025f * sr_);
        fadeStep_ = 1.0f / (0.020f * sr_);
        lpA_      = 1.0f - std::exp(-2.0f * 3.14159265358979f * kSideCornerHz / sr_);
        reset();
    }

    /// Hilo de audio, al inicio de cada bloque.
    void setParams(bool enabled, float amount) noexcept {
        enabled_ = enabled;
        if (std::isfinite(amount)) amount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Modo adaptativo: la profundidad efectiva sigue la densidad de golpes (ver ley arriba).
    void setAuto(bool on) noexcept { auto_ = on; }
    /// Profundidad efectiva ahora (= amount si auto está apagado) — telemetría / tests.
    float effectiveAmount() const noexcept { return auto_ ? amount_ * autoGain_ : amount_; }
    float hitRate() const noexcept { return rate_; }

    bool  isActive() const noexcept { return enabled_ || fade_ > 0.0f; }
    float fade()     const noexcept { return fade_; }
    /// Ganancia lateral actual (1 = sin cambio) — telemetría / tests.
    float sideGain() const noexcept { return g_; }

    void reset() noexcept {
        detector_.reset();
        g_ = 1.0f; hold_ = 0; fade_ = 0.0f;
        rate_ = 0.0f; autoGain_ = 0.4f; prevHit_ = false;
        lp1_ = lp2_ = 0.0f;
    }

    void process(float* L, float* R, std::size_t n) noexcept {
        if (L == nullptr || R == nullptr || n == 0) return;
        if (!enabled_ && fade_ <= 0.0f) {      // bypass bit-exacto
            if (g_ != 1.0f) { g_ = 1.0f; hold_ = 0; lp1_ = lp2_ = 0.0f; detector_.reset(); }
            return;
        }
        std::size_t done = 0;
        while (done < n) {
            const std::size_t c = std::min(n - done, kMaxChunk);
            for (std::size_t i = 0; i < c; ++i) mid_[i] = 0.5f * (L[done + i] + R[done + i]);
            const bool hit = detector_.processBlock(mid_, c);
            if (hit) hold_ = holdLen_;
            {   // densidad de golpes: flancos de subida, media móvil de 2 s
                const float dt = static_cast<float>(c) / sr_;
                const float inst = (hit && !prevHit_) ? (1.0f / dt) : 0.0f;
                rate_ += (inst - rate_) * std::min(1.0f, dt / 2.0f);
                prevHit_ = hit;
                autoGain_ = 0.4f + 0.6f * (1.0f - std::exp(-rate_ / 3.0f));
            }
            const float amt = effectiveAmount();
            const float gTight = 1.0f - kTightPerAmount * amt;
            const float gBloom = 1.0f + kBloomPerAmount * amt;
            for (std::size_t i = 0; i < c; ++i) {
                const float target = (hold_ > 0) ? gTight : gBloom;
                if (hold_ > 0) --hold_;
                g_ += (target - g_) * (target < g_ ? atk_ : rel_);

                if (enabled_) fade_ = std::min(1.0f, fade_ + fadeStep_);
                else          fade_ = std::max(0.0f, fade_ - fadeStep_);

                const float l = L[done + i], r = R[done + i];
                const float s = 0.5f * (l - r);
                lp1_ += (s - lp1_) * lpA_;
                const float h1 = s - lp1_;
                lp2_ += (h1 - lp2_) * lpA_;
                const float hp = h1 - lp2_;                 // lateral > ~250 Hz (12 dB/oct)
                const float s2 = s + fade_ * (g_ - 1.0f) * hp;
                const float m  = 0.5f * (l + r);
                L[done + i] = m + s2;
                R[done + i] = m - s2;
            }
            done += c;
        }
    }

private:
    TransientDetector detector_;
    float sr_ = 48000.0f, atk_ = 0.0f, rel_ = 0.0f, fadeStep_ = 0.0f, lpA_ = 0.0f;
    int   holdLen_ = 0;
    bool  enabled_ = false;
    float amount_ = 0.6f;
    bool  auto_ = false;
    float rate_ = 0.0f, autoGain_ = 0.4f;
    bool  prevHit_ = false;
    float g_ = 1.0f, fade_ = 0.0f, lp1_ = 0.0f, lp2_ = 0.0f;
    int   hold_ = 0;
    float mid_[kMaxChunk] = {};
};

}  // namespace ivanna::dsp
