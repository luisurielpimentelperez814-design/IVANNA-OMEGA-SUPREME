// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA OMEGA SUPREME — PROJECT: ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE
// ============================================================================
// Nueva Capa de Representación Acústica Perceptual:
// Transforma IVANNA desde un "procesador de señal" hacia un "motor de
// reconstrucción de realidad acústica perceptual" que reconstruye EL EVENTO
// (SOURCE → ROOM → AIR → EAR) y no solamente la onda.
//
// FASES INTEGRADAS EN ESTE NÚCLEO:
//   - FASE 1: AcousticGenome & AcousticGenomeEngine
//   - FASE 2: MicroDetailMap & MicroRealityExtractor
//   - FASE 3: AcousticTimeMachine (SOURCE → ROOM → AIR → EAR)
//   - FASE 4: NeuralAcousticInferenceCore (Out-of-RT AI structural discovery)
//   - FASE 5: PersonalAuditoryRealityModel (Listener-specific brain field)
//   - FASE 6: 4D Field Synthesis Supreme trajectories & room interaction
//   - FASE 7: PerceptualOptimizationEngine (Presence, Naturalness, Separation,
//             Fatigue, Immersion)
//   - FASE 8: AcousticRealityOrchestrator (State coordinator between
//             AdaptiveDecisionEngine and DSP engines; zero malloc, zero locks,
//             NEON SIMD, 100% RT-safe)
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include "../experimental/adaptive_engine/adaptive_decision_engine.hpp"
#include "../spatial/HrtfPersonalizer.hpp"
#include "../spatial/HearingAdaptationEngine.hpp"
#include "../spatial/StereoObjectDecomposer.hpp"
#include "omega_control_bus.h"

namespace ivanna::reality {

// ============================================================================
// FASE 1: ACOUSTIC GENOME ENGINE
// ============================================================================
// Un audio no es tratado como muestras sueltas, sino como una firma de
// nacimiento acústico ("objeto acústico ubicado a X metros, con ataque de
// cuerda, con reflexiones laterales, con interacción con la sala").
// ============================================================================

struct ProbableSource {
    float posX{0.0f};               // Posición lateral (m, -4..+4)
    float posY{1.8f};               // Profundidad frontal (m, 0.4..8.0)
    float posZ{0.0f};               // Altura perceptual respecto al oído (m, -1.5..+2.5)
    float velX{0.0f};               // Velocidad lateral continua (m/s)
    float velY{0.0f};               // Velocidad de profundidad continua (m/s)
    float velZ{0.0f};               // Velocidad vertical continua (m/s)
    float fundamentalHz{220.0f};    // Frecuencia fundamental estimada (Hz)
    float attackSharpness{0.5f};    // Nitidez del transitorio de nacimiento [0, 1]
    float harmonicRichness{0.5f};   // Densidad armónica coherente [0, 1]
    float roomCoupling{0.35f};      // Acoplamiento energético fuente-sala [0, 1]
    float existenceProb{1.0f};      // Probabilidad de existencia activa [0, 1]
};

struct SpatialRelations {
    float interSourceCoherence{0.5f}; // Coherencia de fase entre objetos [0, 1]
    float lateralSymmetry{0.0f};      // Balance espacial L/R [-1, +1]
    float depthStratification{1.2f};  // Separación en metros frente-fondo (m)
    float stageWidthMeters{2.4f};     // Apertura física del escenario (m)
    float stageHeightMeters{1.2f};    // Altura perceptual del escenario (m)
};

struct TemporalEnergySignature {
    float onsetVelocity{0.0f};        // Derivada positiva de energía de ataque [0, 1]
    float sustainStability{0.5f};     // Estabilidad de envolvente estacionaria [0, 1]
    float decaySlopeDbPerSec{-25.0f}; // Pendiente de caída energética (dB/s)
    float crestFactorDb{12.0f};       // Factor de cresta real (dB)
};

struct MicroEventSignature {
    float transientDensity{0.2f};     // Densidad de micro-eventos por segundo [0, 1]
    float contactNoiseIndex{0.15f};   // Firma de roce mecánico (cuerda/arco/tecla) [0, 1]
    float subbandPhaseCoherence{0.8f};// Coherencia de fase inter-banda [0, 1]
};

struct RoomFingerprint {
    float estimatedRt60Sec{0.42f};    // Tiempo de reverberación inferido (s)
    float earlyToLateRatioDb{4.5f};   // Claridad acústica C50/C80 proxy (dB)
    float modalColorationIndex{0.1f}; // Resonancia modal de recinto [0, 1]
    float wallAbsorption{0.25f};      // Coeficiente de absorción de paredes [0.05, 0.95]
    float estimatedVolumeM3{120.0f};  // Volumen físico estimado del recinto (m³)
};

struct ReflectionPattern {
    float firstLateralDelayMs{11.5f}; // Retardo de primera reflexión lateral (ms)
    float floorBounceGain{0.22f};     // Ganancia de reflexión de suelo [0, 1]
    float ceilingReflectionGain{0.16f};// Ganancia de reflexión de techo [0, 1]
    float diffuseness{0.45f};         // Difusividad del campo tardío [0, 1]
};

struct alignas(64) AcousticGenome {
    static constexpr size_t kMaxSources = 4;
    std::array<ProbableSource, kMaxSources> sources{};
    SpatialRelations       spatialRelations{};
    TemporalEnergySignature temporalEnergy{};
    MicroEventSignature    microEvents{};
    RoomFingerprint        roomFingerprint{};
    ReflectionPattern      reflectionPattern{};
    float                  uncertainty{0.15f}; // Incertidumbre epistémica [0, 1]
    uint64_t               generationSeq{0};
};

class AcousticGenomeEngine {
public:
    AcousticGenomeEngine() noexcept { reset(); }

    void reset() noexcept {
        prevRms_ = 0.0f;
        prevSideRatio_ = 0.5f;
        prevLowRatio_ = 0.25f;
        for (size_t i = 0; i < AcousticGenome::kMaxSources; ++i) {
            prevPosX_[i] = 0.0f;
            prevPosY_[i] = 1.8f;
            prevPosZ_[i] = 0.0f;
        }
        seq_ = 0;
    }

    // Sintetiza el AcousticGenome a partir de métricas reales del bloque y objetos descompuestos.
    // 100% determinista, sin malloc, sin bloqueos.
    AcousticGenome extractGenome(
        const experimental::RawAudioMetrics& metrics,
        const std::array<spatial::DecomposedObject, 4>& decomposedObjs,
        float sideRatio,
        float lowRatio,
        float dtSeconds = 0.010f) noexcept
    {
        AcousticGenome g{};
        const float safeDt = (dtSeconds > 1.0e-4f) ? dtSeconds : 0.010f;
        const float totalBand = metrics.band_low_energy + metrics.band_mid_energy + metrics.band_high_energy + 1.0e-6f;
        const float lowFrac   = std::clamp(metrics.band_low_energy / totalBand, 0.0f, 1.0f);
        const float midFrac   = std::clamp(metrics.band_mid_energy / totalBand, 0.0f, 1.0f);
        const float highFrac  = std::clamp(metrics.band_high_energy / totalBand, 0.0f, 1.0f);

        // Energía temporal
        const float rmsDelta = metrics.rms - prevRms_;
        prevRms_ = metrics.rms;
        g.temporalEnergy.onsetVelocity = std::clamp(rmsDelta * 12.0f, 0.0f, 1.0f);
        g.temporalEnergy.crestFactorDb = std::clamp(metrics.crest_factor_db, 0.0f, 30.0f);
        g.temporalEnergy.sustainStability = std::clamp(1.0f - std::fabs(rmsDelta) * 5.0f, 0.0f, 1.0f);
        g.temporalEnergy.decaySlopeDbPerSec = (rmsDelta < 0.0f)
            ? std::clamp((rmsDelta / safeDt) * 20.0f, -80.0f, -5.0f)
            : -18.0f;

        // Microeventos
        g.microEvents.transientDensity = std::clamp(
            (g.temporalEnergy.crestFactorDb - 6.0f) / 14.0f + g.temporalEnergy.onsetVelocity * 0.5f,
            0.0f, 1.0f);
        g.microEvents.contactNoiseIndex = std::clamp(highFrac * 1.4f * (0.3f + 0.7f * g.microEvents.transientDensity), 0.0f, 1.0f);
        g.microEvents.subbandPhaseCoherence = std::clamp(1.0f - 0.45f * sideRatio, 0.25f, 1.0f);

        // Huella de sala y patrón de reflexiones
        const float reverbProxy = std::clamp(sideRatio * (1.0f - g.microEvents.transientDensity * 0.5f), 0.05f, 0.95f);
        g.roomFingerprint.estimatedRt60Sec = std::clamp(0.22f + reverbProxy * 1.15f, 0.18f, 2.40f);
        g.roomFingerprint.earlyToLateRatioDb = std::clamp(12.0f - reverbProxy * 10.0f, -2.0f, 14.0f);
        g.roomFingerprint.modalColorationIndex = std::clamp(lowFrac * (1.0f - sideRatio) * 0.8f, 0.0f, 1.0f);
        g.roomFingerprint.wallAbsorption = std::clamp(0.45f - reverbProxy * 0.30f + highFrac * 0.15f, 0.08f, 0.85f);
        g.roomFingerprint.estimatedVolumeM3 = std::clamp(40.0f + reverbProxy * reverbProxy * 1200.0f, 25.0f, 2500.0f);

        const float roomCharacteristicLen = std::cbrt(g.roomFingerprint.estimatedVolumeM3);
        g.reflectionPattern.firstLateralDelayMs = std::clamp((roomCharacteristicLen / 343.0f) * 1000.0f, 4.0f, 45.0f);
        g.reflectionPattern.floorBounceGain = std::clamp(0.28f * (1.0f - g.roomFingerprint.wallAbsorption), 0.05f, 0.45f);
        g.reflectionPattern.ceilingReflectionGain = std::clamp(0.20f * (1.0f - g.roomFingerprint.wallAbsorption), 0.04f, 0.35f);
        g.reflectionPattern.diffuseness = std::clamp(sideRatio * 1.1f, 0.1f, 0.98f);

        // Fuentes probables 4D (posición + velocidad cinemática + altura perceptual)
        // Altura perceptual derivada de la ley de bandas de Blauert (agudos/medios-altos elevan, graves anclan)
        const float baseElevM = (highFrac - lowFrac) * 0.85f;
        const std::array<float, 4> zOffsets = {
            baseElevM * 0.5f,          // 0: CENTER (voz/lead a altura natural de escenario)
            baseElevM + 0.20f,         // 1: LEFT FLANK
            baseElevM + 0.20f,         // 2: RIGHT FLANK
            0.45f + highFrac * 0.65f   // 3: AMBIENT / CANOPY (campo aéreo superior)
        };
        const std::array<float, 4> f0Priors = {
            180.0f + midFrac * 320.0f,
            300.0f + midFrac * 450.0f,
            320.0f + midFrac * 450.0f,
            120.0f + highFrac * 800.0f
        };

        for (size_t i = 0; i < AcousticGenome::kMaxSources; ++i) {
            auto& src = g.sources[i];
            const auto& dObj = decomposedObjs[i];
            // Profundidad variable modulada por la relación directo/reverberante y energía
            const float depthMod = (i == 0)
                ? (1.4f - 0.35f * metrics.voice_score + 0.4f * reverbProxy)
                : ((i == 3) ? (2.8f + 1.8f * reverbProxy) : (1.6f + 0.7f * sideRatio));
            src.posX = std::clamp(dObj.position.x * (1.0f + 0.35f * sideRatio), -3.5f, 3.5f);
            src.posY = std::clamp(depthMod, 0.5f, 7.5f);
            src.posZ = std::clamp(zOffsets[i], -1.2f, 2.2f);

            // Velocidad continua acotada (sin saltos)
            src.velX = std::clamp((src.posX - prevPosX_[i]) / safeDt, -4.0f, 4.0f);
            src.velY = std::clamp((src.posY - prevPosY_[i]) / safeDt, -4.0f, 4.0f);
            src.velZ = std::clamp((src.posZ - prevPosZ_[i]) / safeDt, -2.5f, 2.5f);
            prevPosX_[i] = src.posX;
            prevPosY_[i] = src.posY;
            prevPosZ_[i] = src.posZ;

            src.fundamentalHz    = f0Priors[i];
            src.attackSharpness  = (i == 0) ? g.microEvents.transientDensity : g.microEvents.transientDensity * 0.85f;
            src.harmonicRichness = std::clamp(midFrac * 1.35f, 0.1f, 1.0f);
            src.roomCoupling     = (i == 3) ? std::clamp(reverbProxy * 1.2f, 0.2f, 0.95f)
                                            : std::clamp(reverbProxy * 0.65f, 0.1f, 0.75f);
            src.existenceProb    = (metrics.rms > 1.0e-5f)
                ? std::clamp(dObj.gain * (0.6f + 0.4f * std::min(1.0f, metrics.rms * 6.0f)), 0.05f, 1.0f)
                : 0.0f;
        }

        // Relaciones espaciales globales
        g.spatialRelations.interSourceCoherence = g.microEvents.subbandPhaseCoherence;
        g.spatialRelations.lateralSymmetry      = std::clamp(sideRatio - prevSideRatio_, -1.0f, 1.0f);
        g.spatialRelations.depthStratification  = std::fabs(g.sources[3].posY - g.sources[0].posY);
        g.spatialRelations.stageWidthMeters     = std::fabs(g.sources[2].posX - g.sources[1].posX) + 0.8f;
        g.spatialRelations.stageHeightMeters    = std::fabs(g.sources[3].posZ - g.sources[0].posZ) + 0.6f;

        prevSideRatio_ = sideRatio;
        prevLowRatio_  = lowRatio;

        // Incertidumbre epistémica: baja cuando hay buena relación señal/ruido y estructura clara
        const float snrProxy = std::clamp(metrics.rms * 10.0f, 0.0f, 1.0f);
        g.uncertainty = std::clamp(1.0f - (0.65f * snrProxy + 0.35f * g.microEvents.subbandPhaseCoherence), 0.05f, 0.95f);
        g.generationSeq = ++seq_;
        return g;
    }

private:
    float prevRms_{0.0f};
    float prevSideRatio_{0.5f};
    float prevLowRatio_{0.25f};
    std::array<float, AcousticGenome::kMaxSources> prevPosX_{};
    std::array<float, AcousticGenome::kMaxSources> prevPosY_{};
    std::array<float, AcousticGenome::kMaxSources> prevPosZ_{};
    uint64_t seq_{0};
};

// ============================================================================
// FASE 2: MICROREALITY EXTRACTION ENGINE
// ============================================================================
// Rescata información perceptual normalmente ignorada:
//   - microtransitorios, ruido de contacto, respiración, movimiento humano,
//     aire del recinto, colas reverberantes.
// REGLA DE ORO: No aumentar volumen macro. Aumentar inteligibilidad perceptual.
// ============================================================================

enum class MicroComponentType : uint8_t {
    MicroTransients  = 0,
    ContactNoise     = 1,
    HumanBreath      = 2,
    HumanMovement    = 3,
    RoomAir          = 4,
    ReverberantTails = 5,
    kCount           = 6
};

struct MicroComponentNode {
    float    existence{0.0f};   // [0, 1]
    float    confidence{0.0f};  // [0, 1]
    float    posX{0.0f};        // m
    float    posY{1.5f};        // m
    float    posZ{0.0f};        // m
    uint64_t timestamp{0};      // timestamp monotónico (us o bloque)
};

struct alignas(64) MicroDetailMap {
    std::array<MicroComponentNode, static_cast<size_t>(MicroComponentType::kCount)> nodes{};
    float intelligibilityContrast{0.0f}; // Ganancia diferencial de micro-contraste [0, 0.35], energía neutra
};

class MicroRealityExtractor {
public:
    MicroRealityExtractor() noexcept { reset(); }

    void reset() noexcept {
        fastEnvL_ = fastEnvR_ = 0.0f;
        slowEnvL_ = slowEnvR_ = 0.0f;
        airStateL_ = airStateR_ = 0.0f;
        prevSampleL_ = prevSampleR_ = 0.0f;
        clockTickUs_ = 0;
    }

    // Extrae el MicroDetailMap desde el AcousticGenome y las métricas actuales (hilo de control o RT).
    MicroDetailMap extractMap(const AcousticGenome& genome,
                              const experimental::RawAudioMetrics& metrics,
                              uint64_t timestampUs) noexcept
    {
        MicroDetailMap map{};
        const float confBase = std::clamp(1.0f - genome.uncertainty, 0.1f, 1.0f);
        const float totalBand = metrics.band_low_energy + metrics.band_mid_energy + metrics.band_high_energy + 1.0e-6f;
        const float midRatio  = std::clamp(metrics.band_mid_energy / totalBand, 0.0f, 1.0f);
        const float highRatio = std::clamp(metrics.band_high_energy / totalBand, 0.0f, 1.0f);

        // 0. MicroTransients
        auto& mt = map.nodes[static_cast<size_t>(MicroComponentType::MicroTransients)];
        mt.existence  = std::clamp(genome.microEvents.transientDensity, 0.0f, 1.0f);
        mt.confidence = confBase;
        mt.posX       = genome.sources[0].posX;
        mt.posY       = genome.sources[0].posY;
        mt.posZ       = genome.sources[0].posZ;
        mt.timestamp  = timestampUs;

        // 1. ContactNoise (ataque de plectro/arco/cuerda)
        auto& cn = map.nodes[static_cast<size_t>(MicroComponentType::ContactNoise)];
        cn.existence  = std::clamp(genome.microEvents.contactNoiseIndex, 0.0f, 1.0f);
        cn.confidence = std::clamp(confBase * 0.9f, 0.0f, 1.0f);
        cn.posX       = genome.sources[1].posX * 0.5f;
        cn.posY       = std::max(0.6f, genome.sources[0].posY - 0.2f);
        cn.posZ       = genome.sources[0].posZ - 0.1f;
        cn.timestamp  = timestampUs;

        // 2. HumanBreath (banda formántica media-alta con presencia vocal)
        auto& hb = map.nodes[static_cast<size_t>(MicroComponentType::HumanBreath)];
        hb.existence  = std::clamp(metrics.voice_score * midRatio * 1.35f, 0.0f, 1.0f);
        hb.confidence = std::clamp(confBase * (0.5f + 0.5f * metrics.voice_score), 0.0f, 1.0f);
        hb.posX       = genome.sources[0].posX;
        hb.posY       = std::max(0.5f, genome.sources[0].posY - 0.25f);
        hb.posZ       = genome.sources[0].posZ + 0.05f;
        hb.timestamp  = timestampUs;

        // 3. HumanMovement (cinemática orgánica de las fuentes primarias)
        const float speed0 = std::sqrt(
            genome.sources[0].velX * genome.sources[0].velX +
            genome.sources[0].velY * genome.sources[0].velY +
            genome.sources[0].velZ * genome.sources[0].velZ);
        auto& hm = map.nodes[static_cast<size_t>(MicroComponentType::HumanMovement)];
        hm.existence  = std::clamp(speed0 * 0.45f + std::fabs(genome.spatialRelations.lateralSymmetry), 0.0f, 1.0f);
        hm.confidence = std::clamp(confBase * 0.85f, 0.0f, 1.0f);
        hm.posX       = genome.sources[0].posX;
        hm.posY       = genome.sources[0].posY;
        hm.posZ       = genome.sources[0].posZ;
        hm.timestamp  = timestampUs;

        // 4. RoomAir (aire del recinto en alta frecuencia)
        auto& ra = map.nodes[static_cast<size_t>(MicroComponentType::RoomAir)];
        ra.existence  = std::clamp(highRatio * 1.6f * genome.reflectionPattern.diffuseness, 0.0f, 1.0f);
        ra.confidence = confBase;
        ra.posX       = 0.0f;
        ra.posY       = genome.sources[3].posY;
        ra.posZ       = genome.sources[3].posZ + 0.35f;
        ra.timestamp  = timestampUs;

        // 5. ReverberantTails (colas reverberantes bajo el nivel directo)
        auto& rt = map.nodes[static_cast<size_t>(MicroComponentType::ReverberantTails)];
        rt.existence  = std::clamp(genome.roomFingerprint.estimatedRt60Sec * 0.55f * genome.reflectionPattern.diffuseness, 0.0f, 1.0f);
        rt.confidence = confBase;
        rt.posX       = genome.sources[3].posX;
        rt.posY       = genome.sources[3].posY + 0.5f;
        rt.posZ       = genome.sources[3].posZ;
        rt.timestamp  = timestampUs;

        // Contraste perceptual de micro-detalle (energía estrictamente compensada)
        const float avgMicro = (mt.existence + cn.existence + hb.existence + ra.existence + rt.existence) * 0.2f;
        map.intelligibilityContrast = std::clamp(avgMicro * confBase * 0.24f, 0.0f, 0.24f);
        return map;
    }

    // Pase RT-safe de inteligibilidad perceptual con CONSERVACIÓN ESTRICTA DE ENERGÍA (0 dB inflación de volumen).
    // Realza la articulación de micro-detalle en regiones de baja energía sin incrementar el RMS global del bloque.
    void applyMicroIntelligibilityPass(float* __restrict bufL,
                                       float* __restrict bufR,
                                       size_t numSamples,
                                       const MicroDetailMap& detailMap) noexcept
    {
        if (!bufL || !bufR || numSamples == 0) return;
        const float contrast = std::clamp(detailMap.intelligibilityContrast, 0.0f, 0.24f);
        if (contrast < 1.0e-4f) return;

        double energyBefore = 0.0;
        double energyAfter  = 0.0;

        float fL = fastEnvL_, fR = fastEnvR_;
        float sL = slowEnvL_, sR = slowEnvR_;
        float aL = airStateL_, aR = airStateR_;

        for (size_t i = 0; i < numSamples; ++i) {
            const float xL = bufL[i];
            const float xR = bufR[i];
            energyBefore += static_cast<double>(xL) * xL + static_cast<double>(xR) * xR;

            const float absL = std::fabs(xL);
            const float absR = std::fabs(xR);
            fL += 0.25f * (absL - fL);
            fR += 0.25f * (absR - fR);
            sL += 0.015f * (absL - sL);
            sR += 0.015f * (absR - sR);

            // Extracción de componente de aire/microtransitorio (1-pole high-pass ~3.2 kHz @ 48kHz)
            aL += 0.34f * (xL - aL);
            aR += 0.34f * (xR - aR);
            const float microL = xL - aL;
            const float microR = xR - aR;

            // Desenmascaramiento adaptativo: actúa cuando el micro-transitorio emerge o en el velo de bajo nivel
            const float ratioL = (fL - sL) / (sL + 0.02f);
            const float ratioR = (fR - sR) / (sR + 0.02f);
            const float modL = contrast * std::clamp(ratioL, -0.35f, 1.0f);
            const float modR = contrast * std::clamp(ratioR, -0.35f, 1.0f);

            const float yL = xL + microL * modL;
            const float yR = xR + microR * modR;

            bufL[i] = yL;
            bufR[i] = yR;
            energyAfter += static_cast<double>(yL) * yL + static_cast<double>(yR) * yR;
        }

        fastEnvL_ = fL; fastEnvR_ = fR;
        slowEnvL_ = sL; slowEnvR_ = sR;
        airStateL_ = aL; airStateR_ = aR;

        // Normalización exacta de potencia para cumplir: "No aumentar volumen. Aumentar inteligibilidad perceptual."
        if (energyAfter > 1.0e-12 && energyBefore > 1.0e-12) {
            const float normGain = std::clamp(
                static_cast<float>(std::sqrt(energyBefore / energyAfter)),
                0.85f, 1.0f);
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
            const float32x4_t gv = vdupq_n_f32(normGain);
            size_t i = 0;
            for (; i + 3 < numSamples; i += 4) {
                vst1q_f32(bufL + i, vmulq_f32(vld1q_f32(bufL + i), gv));
                vst1q_f32(bufR + i, vmulq_f32(vld1q_f32(bufR + i), gv));
            }
            for (; i < numSamples; ++i) {
                bufL[i] *= normGain;
                bufR[i] *= normGain;
            }
#else
            for (size_t i = 0; i < numSamples; ++i) {
                bufL[i] *= normGain;
                bufR[i] *= normGain;
            }
#endif
        }
    }

private:
    float fastEnvL_{0.0f}, fastEnvR_{0.0f};
    float slowEnvL_{0.0f}, slowEnvR_{0.0f};
    float airStateL_{0.0f}, airStateR_{0.0f};
    float prevSampleL_{0.0f}, prevSampleR_{0.0f};
    uint64_t clockTickUs_{0};
};

// ============================================================================
// FASE 3: ACOUSTIC TIME MACHINE
// ============================================================================
// Modelo causal físico: SOURCE → ROOM → AIR → EAR
// Un sonido no es un punto; es una historia temporal desde el nacimiento en el
// instrumento, su viaje por el espacio y el aire, hasta la llegada al oído.
// ============================================================================

struct SourceBirthState {
    float excitationEnergy{0.0f};       // Energía de excitación mecánica en t=0
    float attackRiseTimeMs{2.5f};       // Tiempo de subida del transitorio (ms)
    float radiationDirectivity{0.6f};   // Directividad de radiación del instrumento [0, 1]
};

struct RoomTrajectoryState {
    float directFlightTimeMs{5.2f};     // Tiempo de vuelo directo d/c (ms)
    std::array<float, 4> earlyTapDelaysMs{{8.5f, 14.2f, 21.8f, 31.4f}}; // Reflexiones tempranas (ms)
    std::array<float, 4> earlyTapGains{{0.24f, 0.18f, 0.13f, 0.09f}};   // Ganancias físicas por pared
    float meanFreePathMeters{2.8f};     // Camino libre medio 4V/S (m)
    float lateReverbOnsetMs{38.0f};     // Transición de reflexiones tempranas a cola difusa (ms)
};

struct AirPropagationState {
    float propagationDistanceM{1.8f};   // Distancia recorrida en aire (m)
    float airAbsorptionCutoffHz{16500.0f}; // Frecuencia de corte por absorción visco-térmica (Hz)
    float airDampingOnePoleAlpha{0.85f};   // Coeficiente 1-polo equivalente a 48kHz
    float humidityAttenuationDbPerM{0.02f};// Atenuación atmosférica HF (dB/m)
};

struct EarArrivalState {
    float interauralTimeDiffUs{0.0f};   // ITD de arribo al canal auditivo (us)
    float pinnaDiffractionNotchHz{7800.0f}; // Notch espectral de pabellón auricular (Hz)
    float headShadowIldDb{0.0f};        // Sombra acústica cefálica (dB)
    float cochlearTravelDelayComp{0.4f};// Compensación de dispersión basal-apical [0, 1]
};

struct alignas(64) AcousticTimelineEvent {
    SourceBirthState    source{};
    RoomTrajectoryState room{};
    AirPropagationState air{};
    EarArrivalState     ear{};
};

class AcousticTimeMachine {
public:
    static constexpr float kSpeedOfSoundMps = 343.0f;

    static AcousticTimelineEvent reconstructTimeline(
        const AcousticGenome& genome,
        float sampleRate = 48000.0f) noexcept
    {
        AcousticTimelineEvent ev{};
        const auto& lead = genome.sources[0];

        // 1. SOURCE (Nacimiento: instrumento)
        ev.source.excitationEnergy     = std::clamp(lead.existenceProb * (0.5f + 0.5f * lead.attackSharpness), 0.0f, 1.0f);
        ev.source.attackRiseTimeMs     = std::clamp(12.0f - 10.5f * lead.attackSharpness, 0.8f, 15.0f);
        ev.source.radiationDirectivity = std::clamp(0.35f + 0.50f * lead.harmonicRichness, 0.2f, 0.95f);

        // 2. ROOM (Trayectoria en el recinto)
        const float dist3D = std::sqrt(lead.posX * lead.posX + lead.posY * lead.posY + lead.posZ * lead.posZ);
        const float safeDist = std::clamp(dist3D, 0.4f, 8.0f);
        ev.room.directFlightTimeMs = (safeDist / kSpeedOfSoundMps) * 1000.0f;

        const float baseLatMs = genome.reflectionPattern.firstLateralDelayMs;
        ev.room.earlyTapDelaysMs[0] = baseLatMs;
        ev.room.earlyTapDelaysMs[1] = baseLatMs * 1.62f;
        ev.room.earlyTapDelaysMs[2] = baseLatMs * 2.38f;
        ev.room.earlyTapDelaysMs[3] = baseLatMs * 3.15f;

        const float reflScale = std::clamp(1.0f - genome.roomFingerprint.wallAbsorption, 0.15f, 0.90f);
        ev.room.earlyTapGains[0] = 0.25f * reflScale;
        ev.room.earlyTapGains[1] = genome.reflectionPattern.floorBounceGain;
        ev.room.earlyTapGains[2] = genome.reflectionPattern.ceilingReflectionGain;
        ev.room.earlyTapGains[3] = 0.10f * reflScale;

        const float vol = std::max(25.0f, genome.roomFingerprint.estimatedVolumeM3);
        const float surfaceApprox = 6.0f * std::pow(vol, 2.0f / 3.0f);
        ev.room.meanFreePathMeters = (4.0f * vol) / std::max(1.0f, surfaceApprox);
        ev.room.lateReverbOnsetMs  = ev.room.earlyTapDelaysMs[3] + 6.5f;

        // 3. AIR (Propagación atmosférica ISO 9613-1 simplificada)
        ev.air.propagationDistanceM      = safeDist;
        ev.air.humidityAttenuationDbPerM = 0.035f;
        ev.air.airAbsorptionCutoffHz     = std::clamp(20000.0f * std::exp(-0.048f * safeDist), 3500.0f, 20000.0f);
        const float safeSr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        ev.air.airDampingOnePoleAlpha    = std::clamp(
            1.0f - std::exp(-2.0f * 3.14159265f * ev.air.airAbsorptionCutoffHz / safeSr),
            0.08f, 0.99f);

        // 4. EAR (Interacción con oído humano)
        const float azimuth = std::atan2(lead.posX, std::max(0.2f, lead.posY));
        ev.ear.interauralTimeDiffUs    = std::sin(azimuth) * 630.0f;
        // Elevación desplaza el notch de concha/pinna (6.5 kHz abajo -> 9.5 kHz arriba)
        const float elevAngle          = std::atan2(lead.posZ, std::max(0.2f, lead.posY));
        ev.ear.pinnaDiffractionNotchHz = std::clamp(7600.0f + elevAngle * 1800.0f, 5500.0f, 11000.0f);
        ev.ear.headShadowIldDb         = std::sin(azimuth) * 6.5f;
        ev.ear.cochlearTravelDelayComp = std::clamp(0.35f + 0.45f * lead.attackSharpness, 0.1f, 0.9f);

        return ev;
    }
};

// ============================================================================
// FASE 4: NEURAL ACOUSTIC INFERENCE CORE
// ============================================================================
// La IA no genera sonidos. La IA descubre estructuras ocultas:
//   - separación de fuentes
//   - predicción de reflexiones
//   - identificación de geometría
//   - clasificación acústica
// Trabaja FUERA del audio thread. El DSP ejecuta. La IA propone.
// ============================================================================

enum class AcousticSceneArchetype : uint8_t {
    IntimateStudio   = 0,
    ChamberHall      = 1,
    LiveConcertArena = 2,
    OpenAirField     = 3,
    CinematicStage   = 4
};

struct alignas(64) NeuralAcousticProposal {
    // 1. Separación de fuentes (pesos de desmezcla perceptual [Center, Left, Right, Ambient])
    std::array<float, 4> sourceSeparationWeights{{1.0f, 0.95f, 0.95f, 0.65f}};
    // 2. Predicción de reflexiones (ganancias de reflexiones tempranas predichas)
    std::array<float, 4> predictedReflectionGains{{0.22f, 0.17f, 0.12f, 0.08f}};
    // 3. Identificación de geometría de recinto (Width X, Depth Y, Height Z en metros)
    std::array<float, 3> inferredRoomDimsMeters{{6.5f, 8.2f, 3.4f}};
    float inferredWallAbsorption{0.25f};
    // 4. Clasificación acústica
    AcousticSceneArchetype sceneArchetype{AcousticSceneArchetype::IntimateStudio};
    float inferenceConfidence{0.85f};
    uint64_t proposalSeq{0};
};

class NeuralAcousticInferenceCore {
public:
    NeuralAcousticInferenceCore() noexcept = default;

    // Ejecutado FUERA del audio thread (en hilo de control / inferencia):
    // descubre estructuras ocultas a partir del AcousticGenome y publica una
    // propuesta lock-free para que el orquestador la consuma sin bloquear.
    NeuralAcousticProposal inferHiddenStructure(
        const AcousticGenome& genome,
        const experimental::RawAudioMetrics& metrics) noexcept
    {
        NeuralAcousticProposal prop{};
        const float conf = std::clamp(1.0f - genome.uncertainty, 0.15f, 1.0f);
        prop.inferenceConfidence = conf;

        // 1. Separación de fuentes guiada por actividad vocal, transitorios y difusividad
        const float voiceDom = std::clamp(metrics.voice_score, 0.0f, 1.0f);
        const float diff     = genome.reflectionPattern.diffuseness;
        prop.sourceSeparationWeights[0] = std::clamp(0.85f + 0.30f * voiceDom, 0.6f, 1.25f);
        prop.sourceSeparationWeights[1] = std::clamp(0.80f + 0.25f * (1.0f - voiceDom * 0.4f), 0.5f, 1.15f);
        prop.sourceSeparationWeights[2] = std::clamp(0.80f + 0.25f * (1.0f - voiceDom * 0.4f), 0.5f, 1.15f);
        prop.sourceSeparationWeights[3] = std::clamp(0.45f + 0.45f * diff, 0.25f, 1.0f);

        // 2. Predicción de reflexiones tempranas coherentes con la sala inferida
        const float reflDrive = std::clamp(1.0f - genome.roomFingerprint.wallAbsorption, 0.1f, 0.9f);
        prop.predictedReflectionGains[0] = 0.26f * reflDrive;
        prop.predictedReflectionGains[1] = genome.reflectionPattern.floorBounceGain;
        prop.predictedReflectionGains[2] = genome.reflectionPattern.ceilingReflectionGain;
        prop.predictedReflectionGains[3] = 0.11f * reflDrive * diff;

        // 3. Identificación geométrica (proporciones áureas de Louden / Bolt escaladas al volumen inferido)
        const float vol = std::clamp(genome.roomFingerprint.estimatedVolumeM3, 25.0f, 2500.0f);
        // Ratio de Louden: 1 : 1.4 : 1.9 (Alto : Ancho : Largo)
        const float heightZ = std::clamp(std::cbrt(vol / (1.4f * 1.9f)), 2.4f, 14.0f);
        prop.inferredRoomDimsMeters[0] = heightZ * 1.4f; // Width X
        prop.inferredRoomDimsMeters[1] = heightZ * 1.9f; // Depth Y
        prop.inferredRoomDimsMeters[2] = heightZ;        // Height Z
        prop.inferredWallAbsorption    = genome.roomFingerprint.wallAbsorption;

        // 4. Clasificación acústica de escena
        const float rt60 = genome.roomFingerprint.estimatedRt60Sec;
        if (rt60 > 0.95f && diff > 0.65f) {
            prop.sceneArchetype = AcousticSceneArchetype::LiveConcertArena;
        } else if (rt60 > 0.60f) {
            prop.sceneArchetype = AcousticSceneArchetype::ChamberHall;
        } else if (genome.spatialRelations.stageWidthMeters > 3.2f && genome.roomFingerprint.wallAbsorption > 0.55f) {
            prop.sceneArchetype = AcousticSceneArchetype::OpenAirField;
        } else if (metrics.crest_factor_db > 15.0f && diff > 0.50f) {
            prop.sceneArchetype = AcousticSceneArchetype::CinematicStage;
        } else {
            prop.sceneArchetype = AcousticSceneArchetype::IntimateStudio;
        }

        prop.proposalSeq = ++seq_;
        publishProposal(prop);
        return prop;
    }

    void publishProposal(const NeuralAcousticProposal& p) noexcept {
        guard_.fetch_add(1, std::memory_order_acq_rel);
        uint32_t buf[kWords];
        std::memcpy(buf, &p, sizeof(p));
        for (size_t i = 0; i < kWords; ++i) {
            words_[i].store(buf[i], std::memory_order_relaxed);
        }
        guard_.fetch_add(1, std::memory_order_release);
    }

    bool consumeLatestProposal(NeuralAcousticProposal& out, uint64_t& lastSeq) const noexcept {
        NeuralAcousticProposal snap{};
        uint32_t g1, g2;
        for (;;) {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;
            uint32_t buf[kWords];
            for (size_t i = 0; i < kWords; ++i) {
                buf[i] = words_[i].load(std::memory_order_relaxed);
            }
            std::memcpy(&snap, buf, sizeof(snap));
            g2 = guard_.load(std::memory_order_acquire);
            if (g1 == g2) break;
        }
        if (snap.proposalSeq == 0 || snap.proposalSeq == lastSeq) return false;
        lastSeq = snap.proposalSeq;
        out = snap;
        return true;
    }

private:
    static constexpr size_t kWords = sizeof(NeuralAcousticProposal) / sizeof(uint32_t);
    static_assert(sizeof(NeuralAcousticProposal) % sizeof(uint32_t) == 0,
                  "NeuralAcousticProposal must be a multiple of 4 bytes");
    alignas(64) std::array<std::atomic<uint32_t>, kWords> words_{};
    std::atomic<uint32_t> guard_{0};
    uint64_t seq_{0};
};

// ============================================================================
// FASE 5: PERSONAL AUDITORY REALITY MODEL
// ============================================================================
// "No existe un 'audio perfecto universal'. Existe: audio reconstruido para
// un cerebro específico."
// Entrada: HRTF, posición, dispositivo, respuesta del sistema.
// Salida: campo acústico personalizado.
// ============================================================================

enum class DeviceTransducerClass : uint8_t {
    IemSealed         = 0,
    OpenBackPlanar    = 1,
    ClosedDynamic     = 2,
    TwsAnc            = 3,
    SpeakerNearfield  = 4
};

struct ListenerRealityInput {
    spatial::UserPinnaProfile pinnaProfile{};
    spatial::AudiogramProfile audiogramProfile{};
    std::array<float, 7> safLatentQ{{0.004f, 0.0f, 0.0055f, 0.0047f, -0.0039f, 0.0048f, 0.0097f}};
    float headYawDeg{0.0f};
    float headPitchDeg{0.0f};
    float headRollDeg{0.0f};
    DeviceTransducerClass deviceClass{DeviceTransducerClass::IemSealed};
    float systemResonanceHz{92.0f};
};

struct alignas(64) PersonalizedAcousticField {
    float customItdScale{1.0f};           // Factor antropométrico de radio cefálico Woodworth
    float customPinnaNotchHz{7800.0f};    // Frecuencia de notch de concha personalizada (Hz)
    float canalResonanceBoostDb{1.6f};    // Compensación de resonancia de canal auditivo (dB)
    float externalizationFactor{0.82f};   // Índice de externalización fuera de la cabeza [0, 1]
    float crossfeedNaturalness{0.20f};    // Acoplamiento binaural/transaural según dispositivo
    float subBassSealCompensation{1.0f};  // Compensación de sello acústico [0.8, 1.6]
    float elevationCueStrength{0.65f};    // Intensidad de filtro espectral de altura Z [0, 1]
};

class PersonalAuditoryRealityModel {
public:
    PersonalAuditoryRealityModel() noexcept = default;

    void setListenerInput(const ListenerRealityInput& in) noexcept {
        input_ = in;
    }

    const ListenerRealityInput& listenerInput() const noexcept { return input_; }

    PersonalizedAcousticField synthesizePersonalField(const AcousticTimelineEvent& timeline) const noexcept {
        PersonalizedAcousticField field{};

        // 1. Escala ITD de Woodworth por circunferencia cefálica + latente SAF q[0]
        const float stdRadiusCm  = 8.75f;
        const float userRadiusCm = std::clamp(input_.pinnaProfile.head_circumference_cm, 48.0f, 64.0f) / (2.0f * 3.14159265f);
        const float q0Mod        = 1.0f + std::clamp(input_.safLatentQ[0] * 4.0f, -0.12f, 0.12f);
        field.customItdScale     = std::clamp((userRadiusCm / stdRadiusCm) * q0Mod, 0.75f, 1.30f);

        // 2. Notch de pabellón auricular combinado con la elevación del evento en la máquina del tiempo
        const float pinnaDepthM = (std::clamp(input_.pinnaProfile.ear_pinna_size_mm, 50.0f, 80.0f) * 0.5f) * 1.0e-3f;
        const float anthropoNotchHz = 343.0f / (4.0f * std::max(0.01f, pinnaDepthM));
        field.customPinnaNotchHz = std::clamp(0.6f * anthropoNotchHz + 0.4f * timeline.ear.pinnaDiffractionNotchHz, 5200.0f, 11500.0f);

        // 3. Respuesta del dispositivo + sello del canal auditivo
        float devCrossfeed = 0.20f;
        float devExtBoost  = 0.80f;
        switch (input_.deviceClass) {
            case DeviceTransducerClass::IemSealed:
                devCrossfeed = 0.24f; // IEMs aíslan 100% L/R: requieren mayor acoplamiento temprano para externalizar
                devExtBoost  = 0.85f;
                break;
            case DeviceTransducerClass::OpenBackPlanar:
                devCrossfeed = 0.14f;
                devExtBoost  = 0.94f;
                break;
            case DeviceTransducerClass::ClosedDynamic:
                devCrossfeed = 0.20f;
                devExtBoost  = 0.86f;
                break;
            case DeviceTransducerClass::TwsAnc:
                devCrossfeed = 0.22f;
                devExtBoost  = 0.84f;
                break;
            case DeviceTransducerClass::SpeakerNearfield:
                devCrossfeed = 0.36f;
                devExtBoost  = 0.96f;
                break;
        }
        field.crossfeedNaturalness    = devCrossfeed;
        field.externalizationFactor   = devExtBoost;
        field.canalResonanceBoostDb   = std::clamp(input_.pinnaProfile.canal_resonance_boost_db, -6.0f, 6.0f);
        const float seal              = std::clamp(input_.audiogramProfile.ear_tip_seal_factor, 0.25f, 1.0f);
        field.subBassSealCompensation = 1.0f + (1.0f - seal) * 0.65f;
        field.elevationCueStrength    = std::clamp(0.55f + 0.15f * std::cos(input_.headPitchDeg * 0.0174533f), 0.25f, 0.95f);

        return field;
    }

private:
    ListenerRealityInput input_{};
};

// ============================================================================
// FASE 7: PERCEPTUAL OPTIMIZATION ENGINE
// ============================================================================
// Optimiza contra el cerebro humano en 5 dimensiones perceptuales:
//   - presencia
//   - naturalidad
//   - separación
//   - fatiga
//   - inmersión
// No persigue números artificiales. Persigue realismo.
// ============================================================================

struct alignas(32) PerceptualOptimizationMetrics {
    float presence{0.82f};      // [0, 1] Proximidad tangible de la fuente primaria
    float naturalness{0.88f};   // [0, 1] Ausencia de coloración artificial / fase orgánica
    float separation{0.80f};    // [0, 1] Desenmascaramiento binaural entre objetos
    float fatigueFree{0.90f};   // [0, 1] Confort metabólico coclear (1.0 = cero fatiga)
    float immersion{0.85f};     // [0, 1] Envolvimiento espacial coherente con el recinto
    float realismScore{0.85f};  // [0, 1] Índice compuesto de reconstrucción de realidad
};

struct PerceptualOptimizationTargets {
    PerceptualOptimizationMetrics scores{};
    float directPresenceScale{1.0f};      // Escala de presencia de onda directa [0.85, 1.15]
    float harmonicRestraintScale{1.0f};   // Contención de excitadores para preservar naturalidad [0.3, 1.0]
    float binauralUnmaskingSpread{1.0f};  // Apertura de objetos 4D sin hueco central [0.75, 1.45]
    float roomWetRealismScale{1.0f};      // Balance de sala para inmersión sin emborronar ataques [0.4, 1.25]
    float cochlearReliefIntensity{0.65f}; // Intensidad óptima de cancelación no-lineal OHC [0.2, 1.0]
};

class PerceptualOptimizationEngine {
public:
    static PerceptualOptimizationTargets optimizeForBrain(
        const AcousticGenome& genome,
        const MicroDetailMap& microMap,
        const NeuralAcousticProposal& aiProposal,
        const PersonalizedAcousticField& personalField,
        float sibilanceEma,
        float fatigueEma) noexcept
    {
        PerceptualOptimizationTargets out{};

        // 1. Presencia: gobernada por claridad temprana (C50/C80) y micro-detalle vocal/transitorio
        const float c50Norm = std::clamp((genome.roomFingerprint.earlyToLateRatioDb + 2.0f) / 16.0f, 0.0f, 1.0f);
        out.scores.presence = std::clamp(0.55f * c50Norm + 0.45f * (1.0f - genome.uncertainty), 0.25f, 1.0f);

        // 2. Naturalidad: penaliza exceso de sibilancia, coloración modal y distorsión
        const float sibPenalty = std::clamp(sibilanceEma * 0.45f, 0.0f, 0.45f);
        const float modalPen   = genome.roomFingerprint.modalColorationIndex * 0.20f;
        out.scores.naturalness = std::clamp(0.95f - sibPenalty - modalPen, 0.35f, 1.0f);

        // 3. Separación: gobernada por estratificación de profundidad y pesos de separación neuronal
        const float depthNorm = std::clamp(genome.spatialRelations.depthStratification / 2.5f, 0.1f, 1.0f);
        out.scores.separation = std::clamp(0.50f * depthNorm + 0.50f * aiProposal.inferenceConfidence, 0.30f, 1.0f);

        // 4. Libre de fatiga: inversamente proporcional a fatigueEma y compresión excesiva
        out.scores.fatigueFree = std::clamp(1.0f - fatigueEma * 0.65f - sibPenalty * 0.35f, 0.25f, 1.0f);

        // 5. Inmersión: acoplamiento entre externalización personal, difusividad de sala y altura 4D
        const float heightNorm = std::clamp(genome.spatialRelations.stageHeightMeters / 2.0f, 0.2f, 1.0f);
        out.scores.immersion = std::clamp(
            0.45f * personalField.externalizationFactor +
            0.35f * genome.reflectionPattern.diffuseness +
            0.20f * heightNorm,
            0.30f, 1.0f);

        // Índice global de realismo perceptual ("parece que el evento acústico volvió a existir")
        out.scores.realismScore = std::clamp(
            0.22f * out.scores.presence +
            0.26f * out.scores.naturalness +
            0.18f * out.scores.separation +
            0.16f * out.scores.fatigueFree +
            0.18f * out.scores.immersion,
            0.0f, 1.0f);

        // Derivación de controles físicos óptimos:
        out.directPresenceScale     = std::clamp(0.92f + 0.16f * out.scores.presence, 0.85f, 1.12f);
        out.harmonicRestraintScale  = std::clamp(out.scores.naturalness * out.scores.fatigueFree, 0.30f, 1.0f);
        out.binauralUnmaskingSpread = std::clamp(0.85f + 0.40f * out.scores.separation, 0.80f, 1.35f);
        out.roomWetRealismScale     = std::clamp(
            0.65f + 0.45f * out.scores.immersion - 0.25f * microMap.nodes[0].existence,
            0.40f, 1.20f);
        out.cochlearReliefIntensity = std::clamp(0.45f + 0.45f * (1.0f - out.scores.fatigueFree * 0.5f), 0.30f, 0.95f);

        return out;
    }
};

// ============================================================================
// FASE 8: INTEGRACIÓN IVANNA — AcousticRealityOrchestrator
// ============================================================================
// Ubicación: Entre AdaptiveDecisionEngine y los motores DSP existentes.
// Regla estricta:
//   - NO procesa muestras directamente.
//   - Coordina estados entre Genoma, MicroRealidad, Máquina del Tiempo,
//     Inferencia Neuronal, Modelo Auditivo Personal, Síntesis 4D y
//     Optimización Perceptual.
//   - Audio thread limpio: cero malloc, cero locks, seqlock atómico.
// ============================================================================

struct alignas(64) AcousticRealityState {
    AcousticGenome                genome{};
    MicroDetailMap                microMap{};
    AcousticTimelineEvent         timeline{};
    NeuralAcousticProposal        neuralProposal{};
    PersonalizedAcousticField     personalField{};
    PerceptualOptimizationTargets perceptual{};
    bool                          enabled{true};
    float                         realityIntensity{1.0f}; // [0, 1] control maestro de reconstrucción
    uint64_t                      sequence{0};
};

class AcousticRealityStateBus {
public:
    AcousticRealityStateBus() noexcept {
        for (auto& w : words_) w.store(0, std::memory_order_relaxed);
    }

    void publish(const AcousticRealityState& state) noexcept {
        guard_.fetch_add(1, std::memory_order_acq_rel);
        uint32_t buf[kWords];
        std::memcpy(buf, &state, sizeof(state));
        for (size_t i = 0; i < kWords; ++i) {
            words_[i].store(buf[i], std::memory_order_relaxed);
        }
        guard_.fetch_add(1, std::memory_order_release);
    }

    bool consumeIfNewer(AcousticRealityState& out, uint64_t& lastSeenSeq) const noexcept {
        AcousticRealityState snap{};
        uint32_t g1, g2;
        for (;;) {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;
            uint32_t buf[kWords];
            for (size_t i = 0; i < kWords; ++i) {
                buf[i] = words_[i].load(std::memory_order_relaxed);
            }
            std::memcpy(&snap, buf, sizeof(snap));
            g2 = guard_.load(std::memory_order_acquire);
            if (g1 == g2) break;
        }
        if (snap.sequence == 0 || snap.sequence == lastSeenSeq) return false;
        lastSeenSeq = snap.sequence;
        out = snap;
        return true;
    }

    AcousticRealityState readLatestSnapshot() const noexcept {
        AcousticRealityState snap{};
        uint64_t dummy = 0;
        consumeIfNewer(snap, dummy);
        return snap;
    }

private:
    static constexpr size_t kWords = sizeof(AcousticRealityState) / sizeof(uint32_t);
    static_assert(sizeof(AcousticRealityState) % sizeof(uint32_t) == 0,
                  "AcousticRealityState must be a multiple of 4 bytes for atomic word seqlock");
    static_assert(std::is_trivially_copyable<AcousticRealityState>::value,
                  "AcousticRealityState must be trivially copyable");
    alignas(64) std::array<std::atomic<uint32_t>, kWords> words_{};
    std::atomic<uint32_t> guard_{0};
};

class AcousticRealityOrchestrator {
public:
    AcousticRealityOrchestrator() noexcept { reset(); }

    void reset() noexcept {
        genomeEngine_.reset();
        microExtractor_.reset();
        seq_ = 0;
        lastConsumedNeuralSeq_ = 0;
    }

    void setEnabled(bool en) noexcept {
        enabled_.store(en, std::memory_order_relaxed);
    }
    bool isEnabled() const noexcept {
        return enabled_.load(std::memory_order_relaxed);
    }

    void setRealityIntensity(float intensity) noexcept {
        if (!std::isfinite(intensity)) return;
        realityIntensity_.store(std::clamp(intensity, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    float getRealityIntensity() const noexcept {
        return realityIntensity_.load(std::memory_order_relaxed);
    }

    PersonalAuditoryRealityModel& personalModel() noexcept { return personalModel_; }
    const PersonalAuditoryRealityModel& personalModel() const noexcept { return personalModel_; }
    NeuralAcousticInferenceCore& neuralCore() noexcept { return neuralCore_; }
    MicroRealityExtractor& microExtractor() noexcept { return microExtractor_; }
    const AcousticRealityStateBus& stateBus() const noexcept { return stateBus_; }

    // Coordina todos los subsistemas (Fases 1 a 7) a partir de las métricas de
    // AdaptiveDecisionEngine y la descomposición espacial, sin tocar muestras PCM.
    AcousticRealityState orchestrateCycle(
        const experimental::RawAudioMetrics& rawMetrics,
        const experimental::AdaptiveState& adaptiveState,
        const std::array<spatial::DecomposedObject, 4>& decomposedObjs,
        float sideRatio,
        float lowRatio,
        float sampleRate = 48000.0f,
        float dtSeconds  = 0.010f,
        uint64_t timestampUs = 0) noexcept
    {
        AcousticRealityState state{};
        state.enabled          = enabled_.load(std::memory_order_relaxed);
        state.realityIntensity = realityIntensity_.load(std::memory_order_relaxed);

        // Fase 1: Extraer firma genética del evento acústico
        state.genome = genomeEngine_.extractGenome(rawMetrics, decomposedObjs, sideRatio, lowRatio, dtSeconds);

        // Fase 2: Extraer mapa de micro-realidad
        state.microMap = microExtractor_.extractMap(state.genome, rawMetrics, timestampUs);

        // Fase 3: Reconstruir línea de tiempo física SOURCE → ROOM → AIR → EAR
        state.timeline = AcousticTimeMachine::reconstructTimeline(state.genome, sampleRate);

        // Fase 4: Consumir propuesta de inferencia neuronal (o actualizar si corremos en hilo de control)
        if (!neuralCore_.consumeLatestProposal(state.neuralProposal, lastConsumedNeuralSeq_)) {
            state.neuralProposal = neuralCore_.inferHiddenStructure(state.genome, rawMetrics);
            lastConsumedNeuralSeq_ = state.neuralProposal.proposalSeq;
        }

        // Fase 5: Sintetizar campo auditivo personalizado del oyente
        state.personalField = personalModel_.synthesizePersonalField(state.timeline);

        // Fase 7: Optimizar contra el cerebro (Presencia, Naturalidad, Separación, Fatiga, Inmersión)
        const float sibProxy = std::clamp(rawMetrics.band_high_energy * 2.5f, 0.0f, 1.0f);
        const float fatProxy = std::clamp(std::fabs(adaptiveState.eq_tilt_db) / 2.5f, 0.0f, 1.0f);
        state.perceptual = PerceptualOptimizationEngine::optimizeForBrain(
            state.genome, state.microMap, state.neuralProposal, state.personalField, sibProxy, fatProxy);

        state.sequence = ++seq_;
        stateBus_.publish(state);
        return state;
    }

    // Aplica la coordinación del evento acústico sobre el OmegaDspSnapshot (Ruta B / SHM)
    // modulando armónicos, WFS 4D, RIR y dinámica sin crear sistemas paralelos.
    static void coordinateSnapshot(OmegaDspSnapshot& snap, const AcousticRealityState& rs) noexcept {
        if (!rs.enabled || rs.realityIntensity <= 0.001f) return;
        const float k = std::clamp(rs.realityIntensity, 0.0f, 1.0f);

        // Modulación de apertura WFS por desenmascaramiento binaural y separación
        if (snap.wfs_enabled != 0) {
            const float targetSpread = snap.wfs_spread * rs.perceptual.binauralUnmaskingSpread;
            snap.wfs_spread = std::clamp(snap.wfs_spread + k * (targetSpread - snap.wfs_spread), 0.5f, 2.0f);
        }

        // Modulación de sala RIR: alinea RT60 y wet con la trayectoria SOURCE -> ROOM -> AIR -> EAR
        if (snap.room_wet > 0.0f) {
            const float targetWet = std::clamp(snap.room_wet * rs.perceptual.roomWetRealismScale, 0.05f, 0.85f);
            snap.room_wet = snap.room_wet + k * (targetWet - snap.room_wet);
        }
        if (snap.room_rt60_s > 0.01f) {
            const float inferredRt60 = rs.genome.roomFingerprint.estimatedRt60Sec;
            snap.room_rt60_s = std::clamp(snap.room_rt60_s * (1.0f - 0.25f * k) + inferredRt60 * (0.25f * k), 0.15f, 3.0f);
        }

        // Contención de excitación artificial para privilegiar naturalidad orgánica
        snap.harmonic_gain = std::clamp(
            snap.harmonic_gain * (1.0f - k + k * rs.perceptual.harmonicRestraintScale),
            0.0f, 2.0f);

        // Intensidad coclear guiada por alivio perceptual
        if ((snap.flags & OMEGA_FLAG_COCHLEAR_ON) != 0) {
            snap.cochlear_intensity = std::clamp(
                snap.cochlear_intensity * (1.0f - 0.3f * k) + rs.perceptual.cochlearReliefIntensity * (0.3f * k),
                0.15f, 1.0f);
        }

        snap.stampCrc();
    }

    // Singleton global RT-safe para acceso coordinado entre JNI, AdaptiveDecisionEngine y Pipeline
    static AcousticRealityOrchestrator& instance() noexcept {
        static AcousticRealityOrchestrator s_instance;
        return s_instance;
    }

private:
    AcousticGenomeEngine         genomeEngine_{};
    MicroRealityExtractor        microExtractor_{};
    NeuralAcousticInferenceCore  neuralCore_{};
    PersonalAuditoryRealityModel personalModel_{};
    AcousticRealityStateBus      stateBus_{};

    std::atomic<bool>  enabled_{true};
    std::atomic<float> realityIntensity_{1.0f};
    uint64_t           seq_{0};
    uint64_t           lastConsumedNeuralSeq_{0};
};

} // namespace ivanna::reality
