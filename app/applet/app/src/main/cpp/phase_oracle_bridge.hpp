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

// C linkage: phase_oracle.cpp exporta esta funcion. Declarada ANTES del
// struct para que este header sea autocontenido (compilable standalone).
#ifdef __cplusplus
extern "C" {
#endif
float phase_oracle_velocity();
#ifdef __cplusplus
}
#endif

namespace ivanna {

// Interfaz read-only al KalmanCubic global de phase_oracle.cpp
// Declarado extern; la definición está en phase_oracle.cpp (g_kalman.state)
struct PhaseOracleBridge {
    // Devuelve |state[1]| normalizado [0,1] como proxy de transient energy.
    // Normalización CALIBRADA (medición host, PhaseKalman3 @ 96 kHz):
    //   ataque 0→0.8  → |vel| pico ≈ 39  (cue ≈ 0.97)
    //   seno 440 Hz @ 0.4 → |vel| ≈ 8.5  (cue ≈ 0.21)
    // La escala vieja 1/5000 venía del filtro anterior (DT=1/384000 con
    // state[1]=1000 fantasma) y mataba el detector: ataque real daba 0.008.
    static inline float transient_cue() noexcept {
        const float vel = phase_oracle_velocity();
        const float abs_vel = vel < 0.f ? -vel : vel;
        // Soft-clip via tanh aproximado para evitar saturación
        constexpr float SCALE = 1.0f / 40.0f;
        const float x = abs_vel * SCALE;
        // fast tanh approx: x*(27+x*x)/(27+9*x*x)
        const float x2 = x * x;
        const float t = x * (27.f + x2) / (27.f + 9.f * x2);
        return t > 1.f ? 1.f : t;
    }
};

} // namespace ivanna

// C linkage: phase_oracle.cpp exporta esto
