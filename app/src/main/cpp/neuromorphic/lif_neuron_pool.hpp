// =====================================================================
//  IVANNA N-P-E  —  LIF Neuron Pool  v1.0.0
//  lif_neuron_pool.hpp
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  Leaky Integrate-and-Fire pool sin malloc/new.
//  Pool estático alineado a 64 bytes para SIMD.
//
//  Ecuación de membrana:
//    τ_m · dV/dt = -(V - V_rest) + R · I(t)
//
//  Integración: Euler explícito con paso dt = 1/Fs
// =====================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace ivannanpe {

// ── Parámetros del modelo LIF ──────────────────────────────────────────────
struct LIFParams {
    float tau_m      = 0.020f;   // Constante de tiempo de membrana [s] (20 ms)
    float v_rest     = -0.070f;  // Potencial de reposo [V] (-70 mV)
    float v_thresh   = -0.055f;  // Umbral de disparo [V] (-55 mV)
    float v_reset    = -0.075f;  // Potencial post-disparo [V] (-75 mV)
    float r_mem      = 10.0f;    // Resistencia de membrana [MΩ → escala normalizada]
    float t_refrac   = 0.002f;   // Periodo refractario [s] (2 ms)
    float sample_rate= 48000.0f;
};

// ── Estado de una neurona individual ──────────────────────────────────────
struct alignas(16) LIFNeuronState {
    float v_mem       = -0.070f; // Potencial de membrana actual
    float refrac_timer= 0.0f;    // Tiempo restante en periodo refractario
    float spike_out   = 0.0f;    // 1.0 si disparó este sample, 0.0 si no
    float _pad        = 0.0f;    // padding para alineación
};

// ── Pool neuronal estático — sin malloc, sin new ───────────────────────────
template<std::size_t N_NEURONS>
class alignas(64) LIFNeuronPool {
public:
    static_assert(N_NEURONS > 0 && (N_NEURONS % 4) == 0,
        "N_NEURONS debe ser múltiplo de 4 para SIMD");

    // Inicializar todos los estados al potencial de reposo
    void reset(const LIFParams& p) noexcept {
        params_ = p;
        dt_     = 1.0f / p.sample_rate;
        for (auto& s : states_) {
            s.v_mem        = p.v_rest;
            s.refrac_timer = 0.0f;
            s.spike_out    = 0.0f;
            s._pad         = 0.0f;
        }
    }

    // ── Procesar un sample para todas las neuronas ─────────────────────────
    // inputs: array de corrientes I(t) de longitud N_NEURONS
    // Retorna: número de disparos en este ciclo
    inline int tick(const std::array<float, N_NEURONS>& inputs) noexcept {
        const float dt        = dt_;
        const float inv_tau   = dt / params_.tau_m;   // dt/τ_m
        const float v_rest    = params_.v_rest;
        const float v_thresh  = params_.v_thresh;
        const float v_reset   = params_.v_reset;
        const float r_mem     = params_.r_mem;
        const float dt_refrac = dt;

        int spikes = 0;

        for (std::size_t i = 0; i < N_NEURONS; ++i) {
            LIFNeuronState& s = states_[i];

            if (s.refrac_timer > 0.0f) {
                // Neurona en periodo refractario — clamp a v_reset
                s.refrac_timer -= dt_refrac;
                s.v_mem         = v_reset;
                s.spike_out     = 0.0f;
                continue;
            }

            // τ_m · dV/dt = -(V - V_rest) + R · I(t)
            // → dV = (dt/τ_m) · [-(V - V_rest) + R·I]
            const float dv = inv_tau * (-(s.v_mem - v_rest) + r_mem * inputs[i]);
            s.v_mem += dv;

            if (s.v_mem >= v_thresh) {
                // Disparo
                s.spike_out     = 1.0f;
                s.v_mem         = v_reset;
                s.refrac_timer  = params_.t_refrac;
                ++spikes;
            } else {
                s.spike_out = 0.0f;
            }
        }
        return spikes;
    }

    // Acceso de solo lectura al estado
    const LIFNeuronState& state(std::size_t i) const noexcept { return states_[i]; }
    constexpr std::size_t size() const noexcept { return N_NEURONS; }

private:
    alignas(64) std::array<LIFNeuronState, N_NEURONS> states_{};
    LIFParams params_{};
    float     dt_{ 1.0f / 48000.0f };
};

// Instancias concretas para IVANNA
using LIFPool32  = LIFNeuronPool<32>;   // Una por banda coclear
using LIFPool128 = LIFNeuronPool<128>;  // Pool de modulación global

} // namespace ivannanpe
