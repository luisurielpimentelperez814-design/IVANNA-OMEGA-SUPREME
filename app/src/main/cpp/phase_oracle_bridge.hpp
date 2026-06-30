// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// phase_oracle_bridge.hpp
// Expone el KalmanCubic de phase_oracle.cpp como fuente de transient cue
// para BiquadEnvelopeBank sin duplicar código ni estado global.
//
// state[0] = posición (sample estimado)
// state[1] = velocidad (derivada = detector de transitorios O(1))
// state[2] = aceleración

#pragma once
#include <atomic>
#include <cstddef>

namespace ivanna {

// Interfaz read-only al KalmanCubic global de phase_oracle.cpp
// Declarado extern; la definición está en phase_oracle.cpp (g_kalman.state)
struct PhaseOracleBridge {
    // Devuelve |state[1]| normalizado [0,1] como proxy de transient energy.
    // Normalización empírica: velocidad pico ~5000 a 48kHz con señal ±1
    static inline float transient_cue() noexcept {
        const float vel = phase_oracle_velocity();
        const float abs_vel = vel < 0.f ? -vel : vel;
        // Soft-clip via tanh aproximado para evitar saturación
        constexpr float SCALE = 1.0f / 5000.0f;
        const float x = abs_vel * SCALE;
        // fast tanh approx: x*(27+x*x)/(27+9*x*x)
        const float x2 = x * x;
        const float t = x * (27.f + x2) / (27.f + 9.f * x2);
        return t > 1.f ? 1.f : t;
    }
};

} // namespace ivanna

// C linkage: phase_oracle.cpp exporta esto
#ifdef __cplusplus
extern "C" {
#endif
float phase_oracle_velocity();
#ifdef __cplusplus
}
#endif
