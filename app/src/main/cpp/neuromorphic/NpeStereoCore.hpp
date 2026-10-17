// NpeStereoCore.hpp
// ============================================================================
// IVANNA — Motor NPE neuromórfico para la RUTA B (módulo Magisk / audioserver)
// ============================================================================
// El NPE (NHO + pool LIF + banco de envolventes coclear + inhibición lateral + compresión OHC + AGC) vivía SOLO en
// la app (jni/ivanna_npe_jni.cpp, Ruta A). Con el módulo instalado y la app procesando por omega_effect, el NPE no
// existía en la cadena: sus controles no tenían efecto. Esta clase es la misma cadena por muestra, sin lo que no es
// seguro en audioserver (mutex de métricas, std::clock, AutonomousBrain, scope, manifold): RT-safe, sin
// asignaciones. Reutiliza NHOEngine, LIFPool32 y BiquadEnvelopeBank tal cual.
//
// Flujo por muestra (idéntico al de Ruta A):
//   x → envolventes (cues L,T,S,R) → LIF(spikes) ; NHO(x) → inhibición lateral (spikes/32, cue S)
//     → compresión OHC (cue L) → ensanche M/S por cue S → AGC lento (10 ms/300 ms, paso acotado)
//     → master gain → rodilla suave 0.8→1.0
//   Mezcla con el original por `fade` (20 ms): encender/apagar sin clic; apagado y fade 0 = BIT-EXACTO.
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "nho_engine.hpp"
#include "lif_neuron_pool.hpp"
#include "biquad_envelope_bank.hpp"

namespace ivanna::neuromorphic {

class NpeStereoCore {
public:
    void prepare(float sampleRate) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        envBank_.init(static_cast<uint32_t>(sr_));
        ivannanpe::LIFParams lp;
        lp.sample_rate = sr_;
        lifPool_.reset(lp);
        aAtt_  = 1.0f - std::exp(-1.0f / (0.010f * sr_));
        aRel_  = 1.0f - std::exp(-1.0f / (0.300f * sr_));
        fadeStep_ = 1.0f / (0.020f * sr_);
        reset();
        nho_.set_harmonic_gain(harmonic_);
    }

    void reset() noexcept {
        nho_.reset();
        envBank_.reset();
        agcGain_ = 1.0f; rms_ = 0.0f; fade_ = 0.0f;
    }

    /// Hilo de audio, al inicio del bloque. Rangos = los de Ruta A.
    void setParams(bool enabled, float harmonicGain, float lateralInhib, float ohcCompression, float masterGainDb) noexcept {
        enabled_ = enabled;
        if (std::isfinite(harmonicGain)) { harmonic_ = std::clamp(harmonicGain, 0.0f, 2.0f); nho_.set_harmonic_gain(harmonic_); }
        if (std::isfinite(lateralInhib))  lateral_ = std::clamp(lateralInhib, 0.0f, 1.0f);
        if (std::isfinite(ohcCompression)) ohc_ = std::clamp(ohcCompression, 0.0f, 1.0f);
        if (std::isfinite(masterGainDb))   masterLin_ = std::pow(10.0f, std::clamp(masterGainDb, -24.0f, 12.0f) * 0.05f);
    }

    bool  isActive() const noexcept { return enabled_ || fade_ > 0.0f; }
    float fade()     const noexcept { return fade_; }
    float agcGain()  const noexcept { return agcGain_; }
    /// Espigas LIF del último bloque (telemetría / pruebas): prueba de que la red neuronal realmente corre.
    int   lastSpikes() const noexcept { return lastSpikes_; }

    void process(float* L, float* R, int n) noexcept {
        if (L == nullptr || R == nullptr || n <= 0) return;
        if (!enabled_ && fade_ <= 0.0f) { return; }                 // bypass bit-exacto
        int spikesBlock = 0;
        for (int i = 0; i < n; ++i) {
            fade_ = enabled_ ? std::min(1.0f, fade_ + fadeStep_) : std::max(0.0f, fade_ - fadeStep_);
            const float xL = L[i], xR = R[i];
            const ivanna::PerceptualCues cues = envBank_.extract(xL, xR);

            std::array<float, 32> currents{};
            for (int b = 0; b < ivanna::BEB_BANDS; ++b) {
                const float iL = envBank_.envL[b] * 0.05f;
                const float iR = envBank_.envR[b] * 0.05f;
                currents[b * 4 + 0] = iL;
                currents[b * 4 + 1] = iR;
                currents[b * 4 + 2] = iL * harmonic_ * 0.5f;
                currents[b * 4 + 3] = iR * harmonic_ * 0.5f;
            }
            const int spikes = lifPool_.tick(currents);
            spikesBlock += spikes;

            float yL, yR;
            nho_.process_sample(xL, xR, yL, yR);

            const float inhib = std::clamp(1.0f - lateral_ * (static_cast<float>(spikes) / 32.0f), 0.2f, 1.0f);
            yL *= inhib; yR *= inhib;

            if (ohc_ > 0.0f) {
                const float g = 1.0f / (1.0f + ohc_ * cues.L * 4.0f);
                yL *= g; yR *= g;
            }
            {   // ensanche M/S dirigido por el cue espacial
                const float mid = 0.5f * (yL + yR), side = 0.5f * (yL - yR);
                const float w = 1.0f + 0.3f * cues.S;
                yL = mid + side * w; yR = mid - side * w;
            }
            // AGC: envolvente del programa (no de la onda) y paso por muestra acotado
            const float inst = 0.5f * (std::fabs(yL) + std::fabs(yR));
            rms_ += (inst > rms_ ? aAtt_ : aRel_) * (inst - rms_);
            const float rmsDb = 20.0f * std::log10(std::max(rms_, 1.0e-6f));
            const float desired = std::pow(10.0f, (kAgcTargetDb - rmsDb) * 0.05f);
            agcGain_ = std::clamp(agcGain_ + std::clamp(kAgcRate * 0.0004f * (desired - agcGain_), -0.0005f, 0.0005f), 0.25f, 4.0f);
            yL *= agcGain_ * masterLin_; yR *= agcGain_ * masterLin_;

            auto knee = [](float x) {
                const float a = std::fabs(x);
                return a <= 0.8f ? x : std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) * 5.0f), x);
            };
            yL = knee(yL); yR = knee(yR);
            if (!std::isfinite(yL)) yL = xL;
            if (!std::isfinite(yR)) yR = xR;
            L[i] = xL + fade_ * (yL - xL);
            R[i] = xR + fade_ * (yR - xR);
        }
        lastSpikes_ = spikesBlock;
    }

private:
    static constexpr float kAgcTargetDb = -18.0f;
    static constexpr float kAgcRate     = 0.3f;
    ivanna::NHOEngine          nho_;
    ivannanpe::LIFPool32       lifPool_;
    ivanna::BiquadEnvelopeBank envBank_;
    float sr_ = 48000.0f, aAtt_ = 0.0f, aRel_ = 0.0f, fadeStep_ = 0.0f;
    bool  enabled_ = false;
    float harmonic_ = 0.2f, lateral_ = 0.2f, ohc_ = 0.3f, masterLin_ = 1.0f;
    float agcGain_ = 1.0f, rms_ = 0.0f, fade_ = 0.0f;
    int   lastSpikes_ = 0;
};

}  // namespace ivanna::neuromorphic
