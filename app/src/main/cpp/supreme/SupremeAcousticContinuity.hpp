#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — SUPREME ACOUSTIC STATE CONTINUITY ARCHITECTURE (C++23)
//
// Paradigma de Procesamiento Acústico Continuo de Nivel Producción:
//   - NIVEL 1: Estado DSP Inmortal (PersistentAcousticState)
//     Toda memoria temporal (IIR, FIR, allpass, Farrow 5º orden, overlap-save FFT,
//     colas RIR, líneas WFS, estados Hilbert/CVNN y biquads cocleares) permanece
//     viva durante toda la reproducción. Prohibido ejecutar clearFilterStates(),
//     reset(), seedConstant() o memset() en el hilo de audio.
//   - NIVEL 2: Suspensión Acústica Inteligente (Soft Suspension)
//     Al retirar un módulo (wet → 0), se conserva el historial acústico real,
//     se congelan parámetros adaptativos no críticos y se mantiene alimentada la
//     frontera temporal sin coste de síntesis pesada.
//   - NIVEL 3: Reactivación Continua (Smooth State Resume)
//     Al reactivar un módulo (wet > 0), retoma instantáneamente desde la historia
//     física real conservada con interpolación progresiva de coeficientes,
//     alineación de fase y validación quirúrgica anti-NaN/denormal.
//
// Garantías RT-Safe (SCHED_FIFO 98):
//   - Lock-free, alignas(64), trivially copyable
//   - Cero malloc / new / delete / std::vector en audio thread
//   - Cero mutex / locks / esperas
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ivanna::supreme {

/**
 * @enum ContinuityPhase
 * @brief Fase operativa del ciclo de continuidad acústica de un motor DSP.
 */
enum class ContinuityPhase : uint8_t {
    Active    = 0, // Motor procesando activamente en mezcla
    Suspending = 1, // En rampa de salida (wet -> 0) preservando estado completo
    Suspended = 2, // En Soft Suspension (wet == 0): estado preservado, 0 CPU pesada
    Resuming  = 3  // En Smooth State Resume: recuperación progresiva desde historia real
};

/**
 * @struct PersistentAcousticState
 * @brief NIVEL 1 — Estado DSP Inmortal.
 *        Descriptor alineado a caché (64 bytes) y trivialmente copiable que
 *        conserva la continuidad de frontera, fase, época y salud numérica
 *        cuando un motor DSP entra o sale temporalmente de la mezcla.
 */
struct alignas(64) PersistentAcousticState {
    uint64_t continuityEpoch{1u};          // Época monotónica (nunca vuelve a 0 en playback)
    uint64_t preservedBlocks{0u};          // Bloques preservados sin destrucción de memoria
    uint32_t sanitizedAnomalies{0u};       // Anomalías NaN/Inf corregidas quirúrgicamente
    uint32_t resumeCount{0u};              // Transiciones Smooth State Resume completadas

    float lastBoundaryL{0.0f};             // Última muestra real observada en canal L
    float lastBoundaryR{0.0f};             // Última muestra real observada en canal R
    float lastDerivativeL{0.0f};           // Derivada de 1er orden en frontera L
    float lastDerivativeR{0.0f};           // Derivada de 1er orden en frontera R

    float preservedPhaseRadL{0.0f};        // Fase instantánea preservada L
    float preservedPhaseRadR{0.0f};        // Fase instantánea preservada R
    float resumeProgress{1.0f};            // Progreso de recuperación suave [0..1]
    float resumeStep{0.004f};              // Paso de recuperación por muestra

    ContinuityPhase phase{ContinuityPhase::Active};
    bool stateValid{true};
    bool softSuspended{false};
    uint8_t reservedPad_[5]{};

    [[gnu::always_inline]] static constexpr bool isHealthySample(float x) noexcept {
        return std::isfinite(x) && (std::fabs(x) > 1.0e-30f || x == 0.0f);
    }

    /**
     * @brief Sanea quirúrgicamente un escalar de estado: preserva cualquier valor
     *        físico válido y únicamente neutraliza NaN/Inf o subnormales extremos
     *        sin destruir el resto de la memoria del filtro.
     */
    [[gnu::always_inline]] inline bool sanitizeScalar(float& v) noexcept {
        if (!std::isfinite(v)) {
            v = 0.0f;
            ++sanitizedAnomalies;
            return false;
        }
        if (v != 0.0f && std::fabs(v) < 1.0e-30f) {
            v = 0.0f;
        }
        return true;
    }

    /**
     * @brief Registra la frontera temporal del bloque de audio actual para mantener
     *        la continuidad de amplitud y primera derivada durante Soft Suspension.
     */
    [[gnu::always_inline]] inline void recordBoundary(
        const float* __restrict left,
        const float* __restrict right,
        size_t numSamples) noexcept
    {
        if (!left || !right || numSamples == 0) return;
        const float endL = std::isfinite(left[numSamples - 1]) ? left[numSamples - 1] : 0.0f;
        const float endR = std::isfinite(right[numSamples - 1]) ? right[numSamples - 1] : 0.0f;
        if (numSamples >= 2) {
            const float prevL = std::isfinite(left[numSamples - 2]) ? left[numSamples - 2] : endL;
            const float prevR = std::isfinite(right[numSamples - 2]) ? right[numSamples - 2] : endR;
            lastDerivativeL = endL - prevL;
            lastDerivativeR = endR - prevR;
        } else {
            lastDerivativeL = endL - lastBoundaryL;
            lastDerivativeR = endR - lastBoundaryR;
        }
        lastBoundaryL = endL;
        lastBoundaryR = endR;
        ++preservedBlocks;
    }
};

static_assert(std::is_trivially_copyable_v<PersistentAcousticState>,
              "PersistentAcousticState must be trivially copyable for lock-free RT operation");
static_assert(alignof(PersistentAcousticState) == 64,
              "PersistentAcousticState must be 64-byte cache-line aligned");
static_assert(sizeof(PersistentAcousticState) == 64,
              "PersistentAcousticState must fit in a single 64-byte cache line");

/**
 * @class SupremeStateContinuityManager
 * @brief Gestor unificado para los Niveles 1, 2 y 3 de la arquitectura de
 *        continuidad acústica:
 *          - preserveState(): conserva historia temporal y fronteras sin borrar filtros.
 *          - suspend(): activa Soft Suspension (congela adaptación no crítica, preserva memoria).
 *          - resume(): activa Smooth State Resume (recuperación progresiva desde historia real).
 *          - validateState(): verifica e higieniza estados IIR/FIR/delay sin destruirlos.
 */
class alignas(64) SupremeStateContinuityManager {
public:
    constexpr SupremeStateContinuityManager() noexcept = default;

    /**
     * @brief Configura la velocidad de recuperación progresiva según el sample rate.
     */
    constexpr void configure(float sampleRate, float resumeMs = 5.0f) noexcept {
        const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        const float samples = std::max(1.0f, sr * 0.001f * std::clamp(resumeMs, 1.0f, 50.0f));
        state_.resumeStep = 1.0f / samples;
    }

    /**
     * @brief NIVEL 1 — Conserva la continuidad de frontera y épocas sin borrar estados.
     */
    [[gnu::always_inline]] inline void preserveState(
        const float* __restrict left = nullptr,
        const float* __restrict right = nullptr,
        size_t numSamples = 0) noexcept
    {
        if (left && right && numSamples > 0) {
            state_.recordBoundary(left, right, numSamples);
        } else {
            ++state_.preservedBlocks;
        }
        state_.stateValid = true;
    }

    /**
     * @brief Conserva las últimas N muestras reales en un historial lineal de tamaño fijo
     *        (ej. línea de retardo de Farrow de 6 muestras) durante Soft Suspension,
     *        garantizando que al reactivar el filtro disponga de la historia real exacta.
     */
    template <size_t N>
    [[gnu::always_inline]] inline void preserveLinearDelayHistory(
        std::array<float, N>& delayLine,
        const float* __restrict input,
        size_t numSamples) noexcept
    {
        if (!input || numSamples == 0) return;
        // En FarrowOrder5Delay, delayLine[0] = x[n], delayLine[1] = x[n-1], ...
        const size_t copyCount = std::min(N, numSamples);
        if (copyCount < N) {
            for (size_t k = N - 1; k >= copyCount; --k) {
                delayLine[k] = delayLine[k - copyCount];
            }
        }
        for (size_t k = 0; k < copyCount; ++k) {
            const float v = input[numSamples - 1 - k];
            delayLine[k] = (std::isfinite(v) && std::fabs(v) > 1.0e-30f) ? v : 0.0f;
        }
    }

    /**
     * @brief NIVEL 2 — Activa Soft Suspension cuando la envolvente wet llega a 0.0f.
     *        Jamas destruye ni pone a cero la memoria de los filtros.
     */
    [[gnu::always_inline]] inline void suspend(
        const float* __restrict left = nullptr,
        const float* __restrict right = nullptr,
        size_t numSamples = 0) noexcept
    {
        if (!state_.softSuspended) {
            state_.softSuspended = true;
            state_.phase = ContinuityPhase::Suspended;
            ++state_.continuityEpoch;
        }
        preserveState(left, right, numSamples);
    }

    /**
     * @brief NIVEL 3 — Activa Smooth State Resume al salir de Soft Suspension.
     *        Recupera progresivamente la influencia del motor sobre su historia real.
     */
    [[gnu::always_inline]] inline void resume() noexcept {
        if (state_.softSuspended || state_.phase == ContinuityPhase::Suspended) {
            state_.softSuspended = false;
            state_.phase = ContinuityPhase::Resuming;
            state_.resumeProgress = 0.0f;
            ++state_.resumeCount;
            ++state_.continuityEpoch;
        } else if (state_.phase != ContinuityPhase::Resuming) {
            state_.phase = ContinuityPhase::Active;
        }
    }

    /**
     * @brief Avanza un paso de recuperación progresiva durante Smooth State Resume.
     */
    [[gnu::always_inline]] inline float nextResumeFactor() noexcept {
        if (state_.phase == ContinuityPhase::Resuming) {
            state_.resumeProgress += state_.resumeStep;
            if (state_.resumeProgress >= 1.0f) {
                state_.resumeProgress = 1.0f;
                state_.phase = ContinuityPhase::Active;
            }
            // Curva Hermite C1 suave para interpolación de coeficientes internos
            const float t = state_.resumeProgress;
            return t * t * (3.0f - 2.0f * t);
        }
        return 1.0f;
    }

    /**
     * @brief Valida la salud numérica del estado interno sin borrar la memoria válida.
     */
    [[gnu::always_inline]] inline bool validateState() noexcept {
        bool ok = true;
        ok = state_.sanitizeScalar(state_.lastBoundaryL) && ok;
        ok = state_.sanitizeScalar(state_.lastBoundaryR) && ok;
        ok = state_.sanitizeScalar(state_.lastDerivativeL) && ok;
        ok = state_.sanitizeScalar(state_.lastDerivativeR) && ok;
        ok = state_.sanitizeScalar(state_.preservedPhaseRadL) && ok;
        ok = state_.sanitizeScalar(state_.preservedPhaseRadR) && ok;
        state_.stateValid = ok;
        return ok;
    }

    /**
     * @brief Valida y sanea quirúrgicamente un arreglo de estado IIR/FIR/allpass
     *        conservando intacta toda muestra finita y eliminando solo NaN/Inf/denormals.
     */
    template <size_t N>
    [[gnu::always_inline]] inline bool validateStateArray(std::array<float, N>& arr) noexcept {
        bool ok = true;
        for (size_t i = 0; i < N; ++i) {
            if (!state_.sanitizeScalar(arr[i])) {
                ok = false;
            }
        }
        state_.stateValid = state_.stateValid && ok;
        return ok;
    }

    [[gnu::always_inline]] inline bool validateRawBuffer(float* __restrict buf, size_t len) noexcept {
        if (!buf) return true;
        bool ok = true;
        for (size_t i = 0; i < len; ++i) {
            if (!state_.sanitizeScalar(buf[i])) {
                ok = false;
            }
        }
        state_.stateValid = state_.stateValid && ok;
        return ok;
    }

    [[nodiscard]] constexpr const PersistentAcousticState& state() const noexcept { return state_; }
    [[nodiscard]] constexpr PersistentAcousticState& state() noexcept { return state_; }
    [[nodiscard]] constexpr bool isSuspended() const noexcept { return state_.softSuspended; }
    [[nodiscard]] constexpr bool isStateValid() const noexcept { return state_.stateValid; }
    [[nodiscard]] constexpr uint64_t continuityEpoch() const noexcept { return state_.continuityEpoch; }
    [[nodiscard]] constexpr uint64_t preservedBlocks() const noexcept { return state_.preservedBlocks; }
    [[nodiscard]] constexpr uint32_t resumeCount() const noexcept { return state_.resumeCount; }

private:
    PersistentAcousticState state_{};
};

static_assert(std::is_trivially_copyable_v<SupremeStateContinuityManager>,
              "SupremeStateContinuityManager must be trivially copyable for lock-free RT operation");
static_assert(alignof(SupremeStateContinuityManager) == 64,
              "SupremeStateContinuityManager must be 64-byte cache-line aligned");

} // namespace ivanna::supreme
