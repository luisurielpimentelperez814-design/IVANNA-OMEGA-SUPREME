// rt_gain_slew.hpp — Limitadores de pendiente para ganancias y parámetros del
// compresor en la ruta caliente de Ruta B (omega_effect.cpp).
//
// © 2026 Luis Uriel Pimentel Pérez — IVANNA OMEGA SUPERME
//
// PROBLEMA (v2.5.384/385, Moto G85): el audio "bombea" (volumen sube/baja) y
// truena aunque HOLOS/ORBE estén en los valores 380. Causa raíz: el objetivo
// de ganancia adaptativa (AdaptiveEngineV2 + ADE) se recalcula POR BLOQUE y
// se aplicaba como rampa lineal DENTRO del bloque desde el valor anterior
// hasta el nuevo objetivo. Si el objetivo salta entre bloques (1.0 -> 0.75
// -> 1.0), la ganancia forma un DIENTE DE SIERRA a ritmo de bloque (~5 ms):
// bombeo audible. Lo mismo con el compresor: el ratio efectivo era
// max(v2Ratio, requestedRatio, adeRatio) sin tope (hasta 20:1) y el umbral
// saltaba con cada análisis de V2 -> el compresor respiraba por bloque.
//
// FIX (v2.5.386): el OBJETIVO mismo se limita en pendiente ENTRE bloques
// (máx 0.02 por bloque ≈ excursión completa en ~130 ms: inaudible como
// bombeo, pero sigue respondiendo) y el ratio efectivo se topa en 4:1.
// Todo aquí es puro y sin estado global: unit-testeable en host.
#pragma once

#include <algorithm>
#include <cmath>

namespace ivanna::rt {

// Mueve `current` hacia `target` como mucho `maxStep` por llamada (un bloque
// de audio). Devuelve el nuevo valor. Si target es NaN se conserva current.
inline float slewLimitValue(float current, float target, float maxStep) noexcept {
    if (!std::isfinite(target)) return current;
    if (!std::isfinite(current)) return target;
    const float d = target - current;
    if (d > maxStep) return current + maxStep;
    if (d < -maxStep) return current - maxStep;
    return target;
}

// Tope de ratio del compresor de Ruta B. La arbitración V2/manual/ADE podía
// entregar hasta 20:1 con umbral de -5.5 dB: compresión brutal que bombea en
// música real. 4:1 es el techo musical seguro para este motor.
inline float capCompressorRatio(float ratio) noexcept {
    if (!std::isfinite(ratio)) return 1.0f;
    return std::clamp(ratio, 1.0f, 4.0f);
}

// Paso máximo por bloque para el objetivo de ganancia adaptativa (0..1).
// 0.02 por bloque de 256 frames @48 kHz (~5.3 ms) = excursión 0.5 en ~130 ms.
inline constexpr float kAdaptiveGainSlewPerBlock = 0.02f;
// Paso máximo por bloque para el umbral del compresor (dB).
inline constexpr float kCompressorThresholdSlewDbPerBlock = 1.0f;
// Paso máximo por bloque para el ratio del compresor.
inline constexpr float kCompressorRatioSlewPerBlock = 0.25f;

} // namespace ivanna::rt
