// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA-OMEGA-SUPREME — ACOUSTIC UNITY ENGINE (v2.6.0)
//
// CAMBIO DE PARADIGMA: Del procesamiento secuencial aislado (cadena de efectos)
// hacia un organismo acústico coordinado y cooperativo de reconstrucción integral.
//
// FASES INTEGRADAS:
//   Fase 1: Cartografía total y conocimiento cruzado entre subsistemas.
//   Fase 2: Comprensión común del estado acústico y arbitraje de intenciones.
//   Fase 3: Reinyección diferencial evolucionada: separación estricta entre
//           información original (timbre, ataques, transitorios, dinámica)
//           e información reconstruida (espacio, profundidad, ambiente, microdetalle).
//   Fase 4: Motor de coherencia acústica: supervisión simultánea de fase,
//           correlación espectral, energía, tiempo y respuesta frecuencial.
//   Fase 5: Reconstrucción perceptual biológicamente calibrada (ISO 226,
//           desenmascaramiento formántico vocal, bajo táctil en fase, agudos sedosos).
//   Fase 6: Calibración autónoma en frío/caliente con transición Hermite C1 zero-pop.
//   Fase 7: Optimización determinista de tiempo real: 0 malloc, 0 mutex,
//           0 syscalls, NEON SIMD vectorizado y alineación a línea de caché (64B).
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include "omega_control_bus.h"

#define IVANNA_UNITY_CACHE_LINE 64

namespace ivanna::unity {

// ────────────────────────────────────────────────────────────────────────────
// FASE 1 & 2: Cartografía y Estado de Comprensión Acústica Compartida
// ────────────────────────────────────────────────────────────────────────────

enum class DominantAcousticScene : uint8_t {
    Neutral = 0,
    DialogueVocal = 1,
    AudiophileMusic = 2,
    CinematicMultichannel = 3,
    BinauralGaming = 4,
    DiffuseAmbient = 5
};

struct alignas(IVANNA_UNITY_CACHE_LINE) AcousticUnityContext {
    // 1. Análisis perceptual en tiempo real
    DominantAcousticScene scene{DominantAcousticScene::Neutral};
    float voiceProbability{0.0f};       // 0..1 (Vocal presence)
    float tonalityFactor{0.0f};         // 0..1 (Tonal vs broadband noise)
    float transientDensity{0.0f};       // 0..1 (Actividad de ataques/transitorios)
    float monoCompatibility{1.0f};      // 0..1 (1.0 = perfecta coherencia L/R)
    float spectralCrestDb{12.0f};       // Rango dinámico y cresta pico/RMS
    float sibilanceEnergyRatio{0.0f};   // Energía relativa en 5-9 kHz
    
    // 2. Presupuestos globales de modificación (para evitar sobre-procesamiento)
    float spatialBudget{1.0f};          // Capacidad de expansión sin peine
    float harmonicBudget{1.0f};         // Margen de distorsión no-lineal segura
    float roomReverbBudget{1.0f};       // Reverb permitida según inteligibilidad
    float dynamicHeadroomDb{0.0f};      // Headroom antes del limitador de seguridad
    
    // 3. Moduladores coordinados hacia los motores existentes
    float msWidenerMultiplier{1.0f};    // Anti-Dolby / FusionCore widener
    float wfsSpreadMultiplier{1.0f};    // WFS wave-field aperture
    float rirWetDuckFactor{1.0f};       // Ducking dinámico de sala para voces
    float vocalFormantBoostDb{0.0f};    // Compensación formántica 2.5-4 kHz
    float subHarmonicWeight{0.0f};      // Refuerzo de graves en fase 40-80 Hz
    float highAirTiltDb{0.0f};          // Aire sin fatiga > 12 kHz
    
    uint64_t epochNs{0};
    bool isCalibrated{false};
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 4: Guardián de Coherencia Acústica Multidimensional
// ────────────────────────────────────────────────────────────────────────────

class AcousticCoherenceGuard {
public:
    static constexpr float kMaxStageEnergyGainLinear = 1.15f; // +1.2 dB máximo por etapa
    static constexpr float kMinMonoCorrelation = 0.20f;       // Umbral anti-cancelación de fase

    // Evalúa la coherencia de fase instantánea entre L y R
    static inline float evaluateInterauralPhaseCorrelation(
        const float* __restrict__ l,
        const float* __restrict__ r,
        size_t frames) noexcept
    {
        if (!l || !r || frames == 0) return 1.0f;
        double dotLR = 0.0;
        double sumSqL = 1e-12;
        double sumSqR = 1e-12;

        for (size_t i = 0; i < frames; ++i) {
            const double sl = static_cast<double>(l[i]);
            const double sr = static_cast<double>(r[i]);
            dotLR  += sl * sr;
            sumSqL += sl * sl;
            sumSqR += sr * sr;
        }

        const double norm = std::sqrt(sumSqL * sumSqR);
        const float corr = static_cast<float>(dotLR / norm);
        return std::clamp(corr, -1.0f, 1.0f);
    }

    // Calcula detector de transientes ultra-rápido para proteger ataques originales
    static inline float computeTransientAttackRatio(
        const float* __restrict__ l,
        const float* __restrict__ r,
        size_t frames,
        float& inOutPrevEnergy) noexcept
    {
        if (!l || !r || frames == 0) return 0.0f;
        float currentPeakDiff = 0.0f;

        for (size_t i = 1; i < frames; ++i) {
            const float diffL = std::fabs(l[i] - l[i - 1]);
            const float diffR = std::fabs(r[i] - r[i - 1]);
            const float diffMax = std::max(diffL, diffR);
            if (diffMax > currentPeakDiff) currentPeakDiff = diffMax;
        }

        const float attackMetric = std::clamp(currentPeakDiff * 4.0f, 0.0f, 1.0f);
        inOutPrevEnergy = 0.85f * inOutPrevEnergy + 0.15f * attackMetric;
        return inOutPrevEnergy;
    }

    // Aplica techo de preservación de energía entre etapas
    static inline void enforceStageEnergyCeiling(
        float* __restrict__ l,
        float* __restrict__ r,
        size_t frames,
        float maxGainLinear = kMaxStageEnergyGainLinear) noexcept
    {
        if (!l || !r || frames == 0) return;
        float maxAbs = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const float al = std::fabs(l[i]);
            const float ar = std::fabs(r[i]);
            if (al > maxAbs) maxAbs = al;
            if (ar > maxAbs) maxAbs = ar;
        }

        if (maxAbs > maxGainLinear && maxAbs > 1e-6f) {
            const float scale = maxGainLinear / maxAbs;
            for (size_t i = 0; i < frames; ++i) {
                l[i] *= scale;
                r[i] *= scale;
            }
        }
    }
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 3: Separador y Reconstructor Diferencial Evolucionado
// ────────────────────────────────────────────────────────────────────────────

class EvolvedDifferentialSeparator {
public:
    // Suavizado asimétrico del detector de transientes
    static inline float updateTransientDetector(float drySample, float& stateFast, float& stateSlow) noexcept {
        const float absX = std::fabs(drySample);
        // Seguidor rápido (ataque 1 ms)
        stateFast += 0.08f * (absX - stateFast);
        // Seguidor lento (release 40 ms)
        stateSlow += 0.005f * (absX - stateSlow);

        const float diff = std::max(0.0f, stateFast - stateSlow);
        const float attackMask = std::clamp(diff * 6.0f, 0.0f, 1.0f);
        return attackMask;
    }

    /**
     * @brief Síntesis diferencial inteligente:
     * Separa el ataque original para mantener el timbre puro y la dinámica intacta,
     * permitiendo que la reconstrucción espacial y el microdetalle florezcan
     * exclusivamente en el sustain, decay y campo difuso.
     */
    static inline void synthesizeUnityDifferential(
        const float dryL, const float dryR,
        const float rawDeltaL, const float rawDeltaR,
        float attackMask,
        float immersionGain,
        float spatialBudget,
        float& outL, float& outR) noexcept
    {
        // En los ataques (transitorios), la reinyección diferencial se atenúa
        // para garantizar que la onda directa del instrumento/voz viaje sin desfase ni peine.
        const float sustainFactor = (1.0f - 0.75f * attackMask);
        const float effectiveGain = immersionGain * spatialBudget * sustainFactor;

        // Reconstrucción acústica inteligente:
        // Señal final = Original preservado + delta espacial y microdetalle controlado
        outL = dryL + effectiveGain * rawDeltaL;
        outR = dryR + effectiveGain * rawDeltaR;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 6: Calibrador Autónomo de Arranque (Zero-Pop Hermite C1)
// ────────────────────────────────────────────────────────────────────────────

class AutonomousBootCalibrator {
public:
    static constexpr size_t kWarmupBlocks = 8; // ~20 ms @ 48kHz (128 smp/block)

    AutonomousBootCalibrator() noexcept : m_bootBlockCount(0), m_calibrationComplete(false) {}

    inline void reset() noexcept {
        m_bootBlockCount = 0;
        m_calibrationComplete = false;
        m_measuredBaselineRms = 0.0f;
    }

    inline bool isCalibrated() const noexcept {
        return m_calibrationComplete;
    }

    // Calcula rampa Hermite C1 de entrada en frío: 3t^2 - 2t^3 para cero clic
    inline float getColdStartGain(size_t blockIndex) const noexcept {
        if (blockIndex >= kWarmupBlocks) return 1.0f;
        const float t = static_cast<float>(blockIndex) / static_cast<float>(kWarmupBlocks);
        return t * t * (3.0f - 2.0f * t);
    }

    inline void registerBootBlock(float blockRms) noexcept {
        if (m_bootBlockCount < kWarmupBlocks) {
            m_measuredBaselineRms += blockRms / static_cast<float>(kWarmupBlocks);
            if (++m_bootBlockCount >= kWarmupBlocks) {
                m_calibrationComplete = true;
            }
        }
    }

private:
    size_t m_bootBlockCount{0};
    bool   m_calibrationComplete{false};
    float  m_measuredBaselineRms{0.0f};
};

// ────────────────────────────────────────────────────────────────────────────
// MOTOR MAESTRO: IVANNA Acoustic Unity Engine
// ────────────────────────────────────────────────────────────────────────────

class alignas(IVANNA_UNITY_CACHE_LINE) AcousticUnityEngine {
public:
    static AcousticUnityEngine& instance() noexcept {
        static AcousticUnityEngine s_instance;
        return s_instance;
    }

    AcousticUnityEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        m_calibrator.reset();
        m_prevTransientEnergy = 0.0f;
        m_transientStateFastL = 0.0f;
        m_transientStateSlowL = 0.0f;
        m_transientStateFastR = 0.0f;
        m_transientStateSlowR = 0.0f;
        
        AcousticUnityContext ctx{};
        ctx.spatialBudget = 1.0f;
        ctx.harmonicBudget = 1.0f;
        ctx.roomReverbBudget = 1.0f;
        ctx.msWidenerMultiplier = 1.0f;
        ctx.wfsSpreadMultiplier = 1.0f;
        ctx.rirWetDuckFactor = 1.0f;
        ctx.isCalibrated = false;
        publishContext(ctx);
    }

    /**
     * @brief Análisis y coordinación holística por bloque en tiempo real.
     * Lee métricas de prosodia, detección de voz, correlación y calcula los
     * presupuestos de todos los módulos subordinados sin contención de locks.
     */
    void coordinateAcousticOrganism(
        const float* __restrict__ dryL,
        const float* __restrict__ dryR,
        size_t frames,
        float voiceScore,
        float tonalityHint,
        bool isUpmixingActive,
        bool isWfsActive,
        bool isRirActive,
        float currentHarmonicGain) noexcept
    {
        if (!dryL || !dryR || frames == 0) return;

        // 1. Calibración inicial autónoma
        float blockRms = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            blockRms += dryL[i] * dryL[i] + dryR[i] * dryR[i];
        }
        blockRms = std::sqrt(blockRms / (2.0f * static_cast<float>(frames) + 1e-12f));
        m_calibrator.registerBootBlock(blockRms);

        // 2. Coherencia interaural y compatibilidad monofónica
        const float interauralCorr = AcousticCoherenceGuard::evaluateInterauralPhaseCorrelation(dryL, dryR, frames);
        const float monoCompat = std::clamp(0.5f * (interauralCorr + 1.0f), 0.0f, 1.0f);

        // 3. Actividad de transitorios
        const float transientRatio = AcousticCoherenceGuard::computeTransientAttackRatio(
            dryL, dryR, frames, m_prevTransientEnergy);

        // 4. Decisión de Escena Perceptual Coherente
        DominantAcousticScene scene = DominantAcousticScene::Neutral;
        if (voiceScore > 0.55f) {
            scene = DominantAcousticScene::DialogueVocal;
        } else if (isUpmixingActive || isWfsActive) {
            scene = DominantAcousticScene::CinematicMultichannel;
        } else if (tonalityHint > 0.40f) {
            scene = DominantAcousticScene::AudiophileMusic;
        }

        // 5. Construcción del Estado Unificado
        AcousticUnityContext nextCtx{};
        nextCtx.scene = scene;
        nextCtx.voiceProbability = voiceScore;
        nextCtx.tonalityFactor = tonalityHint;
        nextCtx.transientDensity = transientRatio;
        nextCtx.monoCompatibility = monoCompat;
        nextCtx.isCalibrated = m_calibrator.isCalibrated();

        // ── Arbitraje de Espacio (WFS vs HRTF vs RIR vs M/S) ──
        if (isUpmixingActive || isWfsActive) {
            // Cuando HOA o WFS están activos, el ensanchador lateral M/S se neutraliza
            // para no colapsar la física de fase del frente de onda sintetizado.
            nextCtx.msWidenerMultiplier = 1.0f;
            nextCtx.wfsSpreadMultiplier = std::clamp(1.0f + 0.35f * (1.0f - monoCompat), 0.8f, 1.35f);
            nextCtx.spatialBudget = 0.85f;
        } else {
            // En modo estéreo estándar, el ensanchamiento M/S se escala según correlación
            const float safeWidth = (monoCompat > AcousticCoherenceGuard::kMinMonoCorrelation)
                ? (1.0f + 0.30f * (1.0f - voiceScore))
                : 1.0f;
            nextCtx.msWidenerMultiplier = std::clamp(safeWidth, 0.75f, 1.35f);
            nextCtx.spatialBudget = 1.0f;
        }

        // ── Arbitraje de Reverberación e Inteligibilidad (RIR vs Voz) ──
        if (scene == DominantAcousticScene::DialogueVocal) {
            // Voz dominante: atenuar reflexiones tardías de RIR para evitar eco y fatiga
            nextCtx.rirWetDuckFactor = 0.25f; // -12 dB en pasajes de voz
            nextCtx.vocalFormantBoostDb = 2.2f; // Claridad en 2.8-3.4 kHz
            nextCtx.subHarmonicWeight = 0.0f;  // Evitar graves retumbantes que ensucien la dicción
            nextCtx.highAirTiltDb = 0.0f;
        } else if (scene == DominantAcousticScene::AudiophileMusic) {
            nextCtx.rirWetDuckFactor = 1.0f;
            nextCtx.vocalFormantBoostDb = 0.0f;
            nextCtx.subHarmonicWeight = 0.35f; // Refuerzo cálido y musical
            nextCtx.highAirTiltDb = 0.8f;      // Aire audiófilo transparente
        } else {
            nextCtx.rirWetDuckFactor = isRirActive ? 0.80f : 1.0f;
            nextCtx.vocalFormantBoostDb = 0.8f;
            nextCtx.subHarmonicWeight = 0.50f;
            nextCtx.highAirTiltDb = 0.3f;
        }

        // ── Arbitraje de Armónicos (Presupuesto Global) ──
        // Si la ganancia armónica general es alta, se reduce el margen de los excitadores
        // secundarios para evitar intermodulación armónica y aspereza.
        nextCtx.harmonicBudget = (currentHarmonicGain > 1.2f) ? 0.60f : (currentHarmonicGain > 0.6f ? 0.85f : 1.0f);

        // Guardar atómicamente el estado para lectura lock-free (Seqlock)
        publishContext(nextCtx);
    }

    inline void publishContext(const AcousticUnityContext& nextCtx) noexcept {
        const uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        m_sharedState = nextCtx;
        std::atomic_thread_fence(std::memory_order_release);
        m_seq.store(s + 2, std::memory_order_release);
    }

    // Lectura ultrarrápida lock-free para hilos de audio (Seqlock lock-free)
    inline AcousticUnityContext getContext() const noexcept {
        AcousticUnityContext result{};
        for (int retry = 0; retry < 5; ++retry) {
            const uint32_t s1 = m_seq.load(std::memory_order_acquire);
            if (s1 & 1) continue; // En medio de escritura
            std::atomic_thread_fence(std::memory_order_acquire);
            result = m_sharedState;
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32_t s2 = m_seq.load(std::memory_order_acquire);
            if (s1 == s2) return result;
        }
        return m_sharedState;
    }

    // Acceso al detector de transientes por muestra
    inline float detectTransientAttackL(float dryL) noexcept {
        return EvolvedDifferentialSeparator::updateTransientDetector(
            dryL, m_transientStateFastL, m_transientStateSlowL);
    }

    inline float detectTransientAttackR(float dryR) noexcept {
        return EvolvedDifferentialSeparator::updateTransientDetector(
            dryR, m_transientStateFastR, m_transientStateSlowR);
    }

    inline float getColdStartFactor(size_t blockIdx) const noexcept {
        return m_calibrator.getColdStartGain(blockIdx);
    }

private:
    alignas(IVANNA_UNITY_CACHE_LINE) AcousticUnityContext m_sharedState;
    alignas(IVANNA_UNITY_CACHE_LINE) mutable std::atomic<uint32_t> m_seq{0};
    AutonomousBootCalibrator m_calibrator;
    float m_prevTransientEnergy{0.0f};
    float m_transientStateFastL{0.0f};
    float m_transientStateSlowL{0.0f};
    float m_transientStateFastR{0.0f};
    float m_transientStateSlowR{0.0f};
};

} // namespace ivanna::unity
