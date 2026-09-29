#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — Supreme Zero-Pop State Transition Layer (C++20/C++23)
// Capa arquitectónica global libre de bloqueos (lock-free), alineada a línea de
// caché (alignas(64)) y trivialmente copiable para eliminar clics, pops,
// tronidos y discontinuidades de amplitud/fase en fronteras de bloque durante:
//   - Activaciones / Desactivaciones (OFF → ON, ON → OFF)
//   - Cambios de parámetros en OmegaControlBus / JNI / SHM (wet = 0 ↔ 1)
//   - Transiciones de niveles térmicos en ThermalGovernor (NORMAL ↔ LIMITED ↔ SAFE/BYPASS)
//
// Garantías de Tiempo Real (SCHED_FIFO RT-Safe):
//   - Cero malloc / new / delete en hilo de audio
//   - Cero std::vector o contenedores dinámicos
//   - Cero mutex / locks / esperas
//   - Cero bypass duro mientras currentGain > 0
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ivanna::supreme {

/**
 * @enum TransitionProfile
 * @brief Perfil temporal de la transición de estado.
 */
enum class TransitionProfile : uint8_t {
    Standard = 0, // Usa attack_ms (si sube) o release_ms (si baja)
    Thermal  = 1, // Usa thermal_ms (20-50 ms) para transiciones de ThermalGovernor
    FastParameter = 2 // Rampa rápida anti-zipper para modulación continua de parámetros
};

/**
 * @struct SupremeTransitionEnvelope
 * @brief Generador de envolvente de transición lineal por muestra con ahorro de CPU
 *        garantizado únicamente cuando la rampa alcanza exactamente cero.
 */
struct alignas(64) SupremeTransitionEnvelope {
    float currentGain{0.0f};
    float targetGain{0.0f};
    float step{0.0f};
    uint32_t remainingSamples{0u};

    float sampleRate{48000.0f};
    float attack_ms{8.0f};   // Recomendado: 5–10 ms para activación OFF → ON
    float release_ms{18.0f}; // Recomendado: 10–25 ms para desactivación ON → OFF
    float thermal_ms{35.0f}; // Recomendado: 20–50 ms para cambios de ThermalGovernor

    uint32_t renderedBlocks{0u};
    uint32_t reservedPad_[7]{};

    /**
     * @brief Configura frecuencia de muestreo y constantes de tiempo en prepare()/init().
     */
    constexpr void configure(
        float sr,
        float attackMs = 8.0f,
        float releaseMs = 18.0f,
        float thermalMs = 35.0f) noexcept
    {
        sampleRate = (sr > 8000.0f) ? sr : 48000.0f;
        attack_ms  = std::clamp(attackMs,  1.0f, 200.0f);
        release_ms = std::clamp(releaseMs, 1.0f, 250.0f);
        thermal_ms = std::clamp(thermalMs, 5.0f, 500.0f);
    }

    /**
     * @brief Fija el estado inicial en constructores/reset() fuera del flujo activo.
     */
    constexpr void setImmediate(float gain) noexcept {
        const float clamped = std::clamp(gain, 0.0f, 1.0f);
        currentGain = clamped;
        targetGain  = clamped;
        step        = 0.0f;
        remainingSamples = 0u;
        renderedBlocks   = 0u;
    }

    /**
     * @brief Actualiza el objetivo de ganancia e inicia una rampa suave si cambió.
     */
    [[gnu::always_inline]] inline void setTarget(
        float desiredTarget,
        TransitionProfile profile = TransitionProfile::Standard) noexcept
    {
        const float clampedTarget = std::isfinite(desiredTarget)
            ? std::clamp(desiredTarget, 0.0f, 1.0f)
            : 0.0f;

        if (std::fabs(clampedTarget - targetGain) <= 1.0e-6f) {
            return;
        }

        targetGain = clampedTarget;
        const float delta = targetGain - currentGain;
        if (std::fabs(delta) <= 1.0e-6f) {
            currentGain = targetGain;
            step = 0.0f;
            remainingSamples = 0u;
            return;
        }

        float durationMs = attack_ms;
        if (profile == TransitionProfile::Thermal) {
            durationMs = thermal_ms;
        } else if (profile == TransitionProfile::FastParameter) {
            durationMs = std::min(attack_ms, 5.0f);
        } else {
            durationMs = (delta > 0.0f) ? attack_ms : release_ms;
        }

        const float rawSamples = durationMs * 0.001f * sampleRate;
        const uint32_t totalSamples = (rawSamples >= 1.0f)
            ? static_cast<uint32_t>(rawSamples + 0.5f)
            : 1u;

        remainingSamples = totalSamples;
        step = delta / static_cast<float>(totalSamples);
    }

    /**
     * @brief Evalúa al inicio de un bloque de audio si el módulo DSP debe ejecutarse.
     *        NUNCA permite bypass duro mientras currentGain > 0 o remainingSamples > 0.
     *        Solo devuelve false cuando la envolvente ha llegado completamente a cero.
     */
    [[gnu::always_inline]] inline bool beginBlock(
        float desiredTarget,
        TransitionProfile profile = TransitionProfile::Standard) noexcept
    {
        setTarget(desiredTarget, profile);
        if (isSilent()) {
            currentGain = 0.0f;
            step = 0.0f;
            return false;
        }
        ++renderedBlocks;
        return true;
    }

    /**
     * @brief Avanza la envolvente una muestra en el hilo de audio (O(1), sin ramas pesadas).
     */
    [[gnu::always_inline]] inline float nextSample() noexcept {
        if (remainingSamples > 0u) {
            --remainingSamples;
            if (remainingSamples == 0u) {
                currentGain = targetGain;
                step = 0.0f;
            } else {
                currentGain = std::clamp(currentGain + step, 0.0f, 1.0f);
            }
        } else {
            currentGain = targetGain;
        }
        return currentGain;
    }

    /**
     * @brief Mezcla lineal libre de clics: dry * (1 - env) + wet * env.
     */
    [[gnu::always_inline]] static constexpr float mixSample(
        float dry, float wet, float env) noexcept
    {
        return dry + env * (wet - dry);
    }

    /**
     * @brief Devuelve true únicamente cuando la transición terminó y la ganancia es cero.
     */
    [[gnu::always_inline]] constexpr bool isSilent() const noexcept {
        return (remainingSamples == 0u) &&
               (currentGain <= 1.0e-6f) &&
               (targetGain <= 1.0e-6f);
    }

    /**
     * @brief Devuelve true mientras una rampa de transición está activa.
     */
    [[gnu::always_inline]] constexpr bool isTransitioning() const noexcept {
        return remainingSamples > 0u;
    }
};

static_assert(std::is_trivially_copyable_v<SupremeTransitionEnvelope>,
              "SupremeTransitionEnvelope must be trivially copyable for lock-free RT operation");
static_assert(alignof(SupremeTransitionEnvelope) == 64,
              "SupremeTransitionEnvelope must be 64-byte cache-line aligned");

} // namespace ivanna::supreme
