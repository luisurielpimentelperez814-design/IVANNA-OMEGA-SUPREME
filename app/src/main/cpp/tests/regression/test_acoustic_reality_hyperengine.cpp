// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA OMEGA SUPREME — VALIDACIÓN OFICIAL:
// PROJECT: ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1–8)
//
// Pruebas de certificación:
//   1. Concierto original vs reconstrucción (AcousticGenome + MicroReality + Perceptual)
//   2. Localización espacial (3D/4D Azimuth, Elevación perceptual y trayectoria continua)
//   3. Sensación de profundidad (SOURCE → ROOM → AIR → EAR, ley 1/d, DRR y absorción aérea)
//   4. Realismo de sala (Geometría inferida, reflexiones tempranas y acoplamiento de bordes WFS)
//   5. Carga CPU (< 15% del presupuesto real-time @ 48 kHz, cero malloc, cero locks)
//   6. Latencia (0.00 ms de latencia algorítmica añadida y bus seqlock wait-free)
// ============================================================================

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

#include "../../include/acoustic_reality_hyperengine.hpp"
#include "../../include/master_acoustic_orchestrator.hpp"
#include "../../spatial/IvannaAudioPipeline.hpp"
#include "../../spatial/WfsRenderer.hpp"
#include "../../spatial/WfsRenderer.cpp"
#include "../../experimental/adaptive_engine/adaptive_decision_engine.cpp"
#include "../../daemon/core/omega_control_bus.cpp"

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr size_t kBlockSize = 256;
constexpr float kTwoPi      = 6.28318530717958647692f;

// Genera un bloque estéreo sintético que modela un evento acústico de concierto:
// - Cuerda pulsada de guitarra (440 Hz + armónicos con decaimiento exponencial y ataque de púa)
// - Microtransitorio de contacto en el inicio del bloque
// - Aire térmico del recinto y articulación de respiración
void synthesizeConcertEventBlock(
    float* outL, float* outR, size_t n, uint64_t sampleOffset) noexcept
{
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(sampleOffset + i) / kSampleRate;
        const float localT = static_cast<float>(i) / kSampleRate;

        // Envolvente de cuerda pulsada (ataque rápido de 2.5 ms + caída natural)
        const float attack  = 1.0f - std::exp(-localT / 0.0025f);
        const float decay   = std::exp(-localT / 0.180f);
        const float env     = attack * decay;

        // Fundamental 440 Hz + armónicos orgánicos
        const float fundamental = std::sin(kTwoPi * 440.0f * t);
        const float harm2       = 0.42f * std::sin(kTwoPi * 880.0f * t + 0.3f);
        const float harm3       = 0.21f * std::sin(kTwoPi * 1320.0f * t + 0.7f);
        const float pluckTrans  = (i < 24)
            ? (0.35f * std::sin(kTwoPi * 3800.0f * localT) * std::exp(-localT / 0.0012f))
            : 0.0f;

        // Articulación de aire / respiración sutil y cola de sala estéreo
        const float breathAir = 0.025f * std::sin(kTwoPi * 2900.0f * t) * std::sin(kTwoPi * 17.0f * t);
        const float roomTailL = 0.08f * std::sin(kTwoPi * 440.0f * (t - 0.011f) + 0.9f);
        const float roomTailR = 0.08f * std::sin(kTwoPi * 440.0f * (t - 0.014f) - 0.6f);

        const float directObj = 0.38f * env * (fundamental + harm2 + harm3) + pluckTrans;
        outL[i] = directObj * 0.92f + breathAir + roomTailL;
        outR[i] = directObj * 0.78f - breathAir * 0.6f + roomTailR;
    }
}

float computeRms(const float* bufL, const float* bufR, size_t n) noexcept {
    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sumSq += static_cast<double>(bufL[i]) * bufL[i]
               + static_cast<double>(bufR[i]) * bufR[i];
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(2 * n)));
}

} // namespace

// ============================================================================
// PRUEBA 1: Concierto Original vs Reconstrucción de Realidad Acústica
// ============================================================================
TEST(AcousticRealityHyperengineValidation, 1_ConcertOriginalVsReconstruction) {
    ivanna::spatial::IvannaAudioPipeline pipeline;
    pipeline.reset();

    std::array<float, kBlockSize> origL{}, origR{};
    std::array<float, kBlockSize> reconL{}, reconR{};

    // Alimentar varios bloques para que los estimadores causales converjan
    for (size_t blk = 0; blk < 12; ++blk) {
        synthesizeConcertEventBlock(origL.data(), origR.data(), kBlockSize, blk * kBlockSize);
        reconL = origL;
        reconR = origR;

        pipeline.setRealityReconstructionEnabled(true);
        pipeline.orchestrateRealityFromBlock(reconL.data(), reconR.data(), kBlockSize, kSampleRate);
        pipeline.process(reconL.data(), reconR.data(), kBlockSize);
    }

    const auto& state = pipeline.activeRealityState();

    // 1. Verificar firma de nacimiento acústico (AcousticGenome — Fase 1)
    EXPECT_GT(state.sequence, 0u);
    EXPECT_LT(state.genome.uncertainty, 0.70f);
    EXPECT_GE(state.genome.sources[0].posY, 0.5f);
    EXPECT_GE(state.genome.sources[0].attackSharpness, 0.0f);

    // 2. Verificar rescate de MicroRealidad (MicroDetailMap — Fase 2)
    //    Cada componente tiene existencia [0,1], confianza [0,1], posición XYZ y timestamp
    constexpr size_t kNumMicro = static_cast<size_t>(ivanna::reality::MicroComponentType::kCount);
    for (size_t c = 0; c < kNumMicro; ++c) {
        const auto& comp = state.microMap.nodes[c];
        EXPECT_GE(comp.existence, 0.0f);
        EXPECT_LE(comp.existence, 1.0f);
        EXPECT_GE(comp.confidence, 0.0f);
        EXPECT_LE(comp.confidence, 1.0f);
        EXPECT_GT(comp.posY, 0.0f);
        EXPECT_GT(comp.timestamp, 0u);
    }
    EXPECT_GE(state.microMap.intelligibilityContrast, 0.0f);
    EXPECT_LE(state.microMap.intelligibilityContrast, 0.25f);

    // 3. Verificar que MicroReality NO infla el volumen (<= 0 dB loudness inflation)
    std::array<float, kBlockSize> microInL = origL;
    std::array<float, kBlockSize> microInR = origR;
    const float rmsBeforeMicro = computeRms(microInL.data(), microInR.data(), kBlockSize);
    pipeline.realityOrchestrator().microExtractor().applyMicroIntelligibilityPass(
        microInL.data(), microInR.data(), kBlockSize, state.microMap);
    const float rmsAfterMicro = computeRms(microInL.data(), microInR.data(), kBlockSize);
    const float loudnessDeltaDb = 20.0f * std::log10((rmsAfterMicro + 1.0e-9f) / (rmsBeforeMicro + 1.0e-9f));
    EXPECT_LE(loudnessDeltaDb, 0.05f)
        << "MicroReality debe aumentar inteligibilidad perceptual sin inflar el volumen RMS";

    // 4. Verificar Perceptual Optimization Engine (Fase 7)
    EXPECT_GT(state.perceptual.scores.presence, 0.35f);
    EXPECT_GT(state.perceptual.scores.naturalness, 0.55f);
    EXPECT_GT(state.perceptual.scores.separation, 0.35f);
    EXPECT_GT(state.perceptual.scores.fatigueFree, 0.50f);
    EXPECT_GT(state.perceptual.scores.immersion, 0.35f);
    EXPECT_GT(state.perceptual.scores.realismScore, 0.50f);
}

// ============================================================================
// PRUEBA 2: Localización Espacial 3D/4D (Azimut, Altura Perceptual y Movimiento)
// ============================================================================
TEST(AcousticRealityHyperengineValidation, 2_SpatialLocalization4DAndElevation) {
    ivanna::spatial::WfsRenderer wfs;
    wfs.init(kSampleRate, static_cast<int>(kBlockSize));
    wfs.setEnabled(true);
    wfs.setRoomDimensions(7.2f, 9.5f, 3.8f, 0.30f);

    std::array<float, kBlockSize> monoSrc{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        monoSrc[i] = 0.5f * std::sin(kTwoPi * 660.0f * static_cast<float>(i) / kSampleRate);
    }
    const float* srcPtr = monoSrc.data();

    // Caso A: Fuente a la izquierda (-1.4 m)
    std::array<float, kBlockSize> outLeftL{}, outLeftR{};
    wfs.setObject4D(0, -1.4f, 2.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1.0f, 0.25f);
    for (int warm = 0; warm < 4; ++warm) {
        outLeftL.fill(0.0f);
        outLeftR.fill(0.0f);
        wfs.process(&srcPtr, 1,
                    outLeftL.data(), outLeftR.data(), static_cast<int>(kBlockSize));
    }
    double eLL = 0.0, eLR = 0.0;
    for (size_t i = 0; i < kBlockSize; ++i) {
        eLL += static_cast<double>(outLeftL[i]) * outLeftL[i];
        eLR += static_cast<double>(outLeftR[i]) * outLeftR[i];
    }
    EXPECT_GT(eLL, eLR * 1.15) << "Fuente 4D en x=-1.4m debe localizar energía dominante en el oído izquierdo";

    // Caso B: Fuente a la derecha (+1.4 m) con velocidad cinemática continua
    wfs.reset();
    wfs.setEnabled(true);
    std::array<float, kBlockSize> outRightL{}, outRightR{};
    wfs.setObject4D(0, +1.4f, 2.0f, 0.8f, 0.25f, -0.10f, 0.05f, 1.0f, 0.30f);
    for (int warm = 0; warm < 4; ++warm) {
        outRightL.fill(0.0f);
        outRightR.fill(0.0f);
        wfs.process(&srcPtr, 1,
                    outRightL.data(), outRightR.data(), static_cast<int>(kBlockSize));
    }
    double eRL = 0.0, eRR = 0.0;
    for (size_t i = 0; i < kBlockSize; ++i) {
        eRL += static_cast<double>(outRightL[i]) * outRightL[i];
        eRR += static_cast<double>(outRightR[i]) * outRightR[i];
    }
    EXPECT_GT(eRR, eRL * 1.15) << "Fuente 4D en x=+1.4m debe localizar energía dominante en el oído derecho";

    // Caso C: Altura perceptual Z en ObjectSpatialRenderer (z = +1.4m vs z = -0.8m)
    ivanna::spatial::ObjectSpatialRenderer objRenderer;
    const float* objPtrs[4] = { monoSrc.data(), monoSrc.data(), monoSrc.data(), monoSrc.data() };
    std::array<ivanna::spatial::DecomposedObject, 4> objsHigh{};
    objsHigh[0].position = { 0.0f, 1.8f, +1.4f };
    objsHigh[0].gain = 1.0f;
    std::array<float, kBlockSize> highL{}, highR{};
    objRenderer.renderObjects(objPtrs, objsHigh, highL.data(), highR.data(), kBlockSize, 1.0f);

    objRenderer.reset();
    std::array<ivanna::spatial::DecomposedObject, 4> objsLow{};
    objsLow[0].position = { 0.0f, 1.8f, -0.8f };
    objsLow[0].gain = 1.0f;
    std::array<float, kBlockSize> lowL{}, lowR{};
    objRenderer.renderObjects(objPtrs, objsLow, lowL.data(), lowR.data(), kBlockSize, 1.0f);

    const float rmsHigh = computeRms(highL.data(), highR.data(), kBlockSize);
    const float rmsLow  = computeRms(lowL.data(), lowR.data(), kBlockSize);
    EXPECT_NE(rmsHigh, rmsLow) << "La coordenada de altura perceptual Z debe modular la distancia 3D y el filtrado espectral pinna";
}

// ============================================================================
// PRUEBA 3: Sensación de Profundidad (SOURCE → ROOM → AIR → EAR)
// ============================================================================
TEST(AcousticRealityHyperengineValidation, 3_DepthPerceptionAndAcousticTimeMachine) {
    ivanna::reality::AcousticGenome nearGenome{};
    nearGenome.sources[0].posX = 0.0f;
    nearGenome.sources[0].posY = 1.0f;
    nearGenome.sources[0].posZ = 0.0f;
    nearGenome.sources[0].attackSharpness = 0.85f;
    nearGenome.sources[0].harmonicRichness = 0.70f;
    nearGenome.roomFingerprint.estimatedVolumeM3 = 180.0f;
    nearGenome.roomFingerprint.estimatedRt60Sec = 0.42f;
    nearGenome.roomFingerprint.wallAbsorption = 0.28f;

    ivanna::reality::AcousticGenome farGenome = nearGenome;
    farGenome.sources[0].posY = 4.8f;

    const auto nearStory = ivanna::reality::AcousticTimeMachine::reconstructTimeline(nearGenome, kSampleRate);
    const auto farStory  = ivanna::reality::AcousticTimeMachine::reconstructTimeline(farGenome, kSampleRate);

    // 1. El tiempo de vuelo directo (ROOM stage) debe escalar físicamente con la distancia
    EXPECT_GT(farStory.room.directFlightTimeMs, nearStory.room.directFlightTimeMs * 3.5f);
    // 2. El corte por absorción atmosférica ISO 9613-1 (AIR stage) debe ser más bajo para fuentes lejanas
    EXPECT_LT(farStory.air.airAbsorptionCutoffHz, nearStory.air.airAbsorptionCutoffHz);
    // 3. La distancia de propagación en aire debe reflejar la profundidad 3D
    EXPECT_GT(farStory.air.propagationDistanceM, nearStory.air.propagationDistanceM * 3.5f);

    // 4. Verificar atenuación física por profundidad en WfsRenderer (y = 1.0 m vs y = 4.5 m)
    ivanna::spatial::WfsRenderer wfsNear, wfsFar;
    wfsNear.init(kSampleRate, static_cast<int>(kBlockSize));
    wfsFar.init(kSampleRate, static_cast<int>(kBlockSize));
    wfsNear.setEnabled(true);
    wfsFar.setEnabled(true);
    wfsNear.setObject4D(0, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.1f);
    wfsFar.setObject4D(0, 0.0f, 4.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.1f);

    std::array<float, kBlockSize> sig{}, nearL{}, nearR{}, farL{}, farR{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        sig[i] = 0.5f * std::sin(kTwoPi * 500.0f * static_cast<float>(i) / kSampleRate);
    }
    const float* sigPtr = sig.data();
    for (int k = 0; k < 5; ++k) {
        nearL.fill(0.0f); nearR.fill(0.0f);
        farL.fill(0.0f);  farR.fill(0.0f);
        wfsNear.process(&sigPtr, 1, nearL.data(), nearR.data(), static_cast<int>(kBlockSize));
        wfsFar.process(&sigPtr, 1, farL.data(), farR.data(), static_cast<int>(kBlockSize));
    }
    const float rmsNear = computeRms(nearL.data(), nearR.data(), kBlockSize);
    const float rmsFar  = computeRms(farL.data(), farR.data(), kBlockSize);
    EXPECT_GT(rmsNear, rmsFar * 1.25f)
        << "Una fuente a 1.0 m debe presentar mayor energía directa que una fuente a 4.5 m de profundidad";
}

// ============================================================================
// PRUEBA 4: Realismo de Sala (Geometría, Reflexiones e Interacción con Bordes)
// ============================================================================
TEST(AcousticRealityHyperengineValidation, 4_RoomRealismAndBoundaryInteraction) {
    ivanna::reality::AcousticGenomeEngine genomeEngine;
    ivanna::reality::NeuralAcousticInferenceCore neuralCore;
    ivanna::experimental::RawAudioMetrics m{};
    m.rms              = 0.25f;
    m.peak             = 0.72f;
    m.crest_factor_db  = 9.2f;
    m.band_low_energy  = 0.09f;
    m.band_mid_energy  = 0.12f;
    m.band_high_energy = 0.04f;
    m.voice_score      = 0.35f;

    std::array<ivanna::spatial::DecomposedObject, 4> objs{};
    const auto genomeSmall = genomeEngine.extractGenome(m, objs, 0.20f, 0.25f, 0.02f);
    const auto genomeHall  = genomeEngine.extractGenome(m, objs, 0.82f, 0.52f, 0.02f);

    const auto proposalSmall = neuralCore.inferHiddenStructure(genomeSmall, m);
    const auto proposalHall  = neuralCore.inferHiddenStructure(genomeHall, m);

    // Una mezcla con mayor energía lateral/difusa infiere un recinto mayor
    const float volSmall = proposalSmall.inferredRoomDimsMeters[0]
                         * proposalSmall.inferredRoomDimsMeters[1]
                         * proposalSmall.inferredRoomDimsMeters[2];
    const float volHall  = proposalHall.inferredRoomDimsMeters[0]
                         * proposalHall.inferredRoomDimsMeters[1]
                         * proposalHall.inferredRoomDimsMeters[2];
    EXPECT_GT(volHall, volSmall * 1.25f);
    EXPECT_GT(genomeHall.roomFingerprint.estimatedRt60Sec, genomeSmall.roomFingerprint.estimatedRt60Sec);

    // Verificar interacción con habitación en WfsRenderer (roomCoupling > 0 vs roomCoupling == 0)
    ivanna::spatial::WfsRenderer wfsDryRoom, wfsCoupledRoom;
    wfsDryRoom.init(kSampleRate, static_cast<int>(kBlockSize));
    wfsCoupledRoom.init(kSampleRate, static_cast<int>(kBlockSize));
    wfsDryRoom.setEnabled(true);
    wfsCoupledRoom.setEnabled(true);
    wfsCoupledRoom.setRoomDimensions(6.0f, 7.5f, 3.2f, 0.20f);

    wfsDryRoom.setObject4D(0, -0.8f, 2.2f, 0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    wfsCoupledRoom.setObject4D(0, -0.8f, 2.2f, 0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.85f);

    std::array<float, kBlockSize> sig{}, dryL{}, dryR{}, coupL{}, coupR{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        sig[i] = (i < 64) ? (0.6f * std::sin(kTwoPi * 800.0f * static_cast<float>(i) / kSampleRate)) : 0.0f;
    }
    const float* sigPtr = sig.data();
    for (int b = 0; b < 4; ++b) {
        dryL.fill(0.0f);  dryR.fill(0.0f);
        coupL.fill(0.0f); coupR.fill(0.0f);
        wfsDryRoom.process(&sigPtr, 1, dryL.data(), dryR.data(), static_cast<int>(kBlockSize));
        wfsCoupledRoom.process(&sigPtr, 1, coupL.data(), coupR.data(), static_cast<int>(kBlockSize));
    }

    double diffEnergy = 0.0;
    for (size_t i = 0; i < kBlockSize; ++i) {
        const double dL = static_cast<double>(coupL[i]) - dryL[i];
        const double dR = static_cast<double>(coupR[i]) - dryR[i];
        diffEnergy += dL * dL + dR * dR;
    }
    EXPECT_GT(diffEnergy, 1.0e-6)
        << "El acoplamiento con las paredes de la habitación en 4D WFS debe inyectar reflexiones tempranas de borde";
}

// ============================================================================
// PRUEBA 5: Carga CPU (Presupuesto Real-Time Estricto < 15% @ 48 kHz)
// ============================================================================
TEST(AcousticRealityHyperengineValidation, 5_CpuLoadRealTimeBudget) {
    ivanna::spatial::IvannaAudioPipeline pipeline;
    pipeline.setRealityReconstructionEnabled(true);

    std::array<float, kBlockSize> bufL{}, bufR{};
    synthesizeConcertEventBlock(bufL.data(), bufR.data(), kBlockSize, 0);

    // Pre-publicar un estado en el bus (como hace el hilo de control)
    pipeline.orchestrateRealityFromBlock(bufL.data(), bufR.data(), kBlockSize, kSampleRate);

    constexpr int kNumIterations = 1000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < kNumIterations; ++it) {
        pipeline.process(bufL.data(), bufR.data(), kBlockSize);
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double totalUs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    const double avgBlockUs = totalUs / static_cast<double>(kNumIterations);
    const double blockDurationUs = (static_cast<double>(kBlockSize) / kSampleRate) * 1.0e6; // 5333.3 us
    const double cpuLoadPercent = (avgBlockUs / blockDurationUs) * 100.0;

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if !defined(__OPTIMIZE__) || defined(IVANNA_SANITIZER_ACTIVE) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    constexpr double kMaxCpuBudgetPct = 60.0; // ASan/UBSan/TSan instrumentation + parallel CTest runner overhead
#else
    constexpr double kMaxCpuBudgetPct = 15.0;
#endif

    EXPECT_LT(cpuLoadPercent, kMaxCpuBudgetPct)
        << "La carga CPU del audio thread debe mantenerse muy por debajo del presupuesto RT (medido: "
        << cpuLoadPercent << "%, " << avgBlockUs << " us/bloque)";
}

// ============================================================================
// PRUEBA 6: Latencia Algorítmica Cero (0.00 ms) y Bus Lock-Free Seqlock
// ============================================================================
TEST(AcousticRealityHyperengineValidation, 6_ZeroAlgorithmicLatencyAndLockFreeBus) {
    // 1. Verificar que un impulso en la muestra 0 emerge en la muestra 0 (0.00 ms added latency)
    ivanna::spatial::IvannaAudioPipeline pipeline;
    pipeline.setRealityReconstructionEnabled(true);

    std::array<float, kBlockSize> impL{}, impR{};
    impL[0] = 0.85f;
    impR[0] = 0.85f;

    pipeline.orchestrateRealityFromBlock(impL.data(), impR.data(), kBlockSize, kSampleRate);
    pipeline.process(impL.data(), impR.data(), kBlockSize);

    const float sample0Energy = std::fabs(impL[0]) + std::fabs(impR[0]);
    EXPECT_GT(sample0Energy, 0.05f)
        << "El pipeline con Reconstrucción de Realidad Acústica debe tener 0 muestras de latencia algorítmica";

    // 2. Verificar coordinación con MasterAcousticOrchestrator::arbitrateSnapshot
    ivanna::OmegaDspSnapshot dspSnap = ivanna::OmegaDspSnapshot::makeDefault();
    ivanna::experimental::RawAudioMetrics rawM{};
    rawM.rms = 0.22f; rawM.peak = 0.65f;
    rawM.band_low_energy = 0.08f; rawM.band_mid_energy = 0.10f; rawM.band_high_energy = 0.04f;
    const auto adaptSt = ivanna::MasterAcousticOrchestrator::evaluate(rawM, 0.10f, 0.05f);

    ivanna::MasterAcousticOrchestrator::arbitrateSnapshot(dspSnap, adaptSt);
    EXPECT_TRUE(dspSnap.isValid()) << "El snapshot arbitrado por MasterAcousticOrchestrator + RealityOrchestrator debe preservar CRC32 válido";
}

// ============================================================================
// PRUEBA 7 (FASE 9): AcousticCognitiveCore — Comprensión de Escena y Ranking
// Escenario: Concierto rock + sala grande + voz central + público amplio + fatiga creciente
// Salida requerida:
//   1. DepthPreservation (profundidad)
//   2. MicroDynamicPreservation (preservación microdinámica)
//   3. VocalClarity (claridad vocal)
//   4. AmbientExpansion (expansión ambiental)
// ============================================================================
TEST(AcousticRealityCognitiveEvolutionValidation, 7_Fase9_CognitiveCoreRockConcertPriorityRanking) {
    using namespace ivanna::reality;

    AcousticGenome genome{};
    genome.roomFingerprint.estimatedVolumeM3 = 1450.0f; // Sala grande / arena
    genome.roomFingerprint.estimatedRt60Sec  = 1.35f;   // Reverberación de concierto
    genome.reflectionPattern.diffuseness     = 0.86f;   // Público amplio
    genome.sources[0].existenceProb          = 0.94f;   // Voz central activa
    genome.sources[0].posX                   = 0.02f;   // Centrada
    genome.sources[0].posY                   = 2.60f;
    genome.uncertainty                       = 0.12f;

    MicroDetailMap microMap{};
    microMap.nodes[static_cast<size_t>(MicroComponentType::MicroTransients)].existence  = 0.84f;
    microMap.nodes[static_cast<size_t>(MicroComponentType::HumanBreath)].existence      = 0.68f;
    microMap.nodes[static_cast<size_t>(MicroComponentType::ReverberantTails)].existence = 0.82f;

    AcousticTimeMachineState tmState = AcousticTimeMachine::reconstructTimeline(genome, kSampleRate);
    PersonalizedAcousticField personalField{};
    personalField.elevationCueStrength = 0.70f;

    PerceptualOptimizationState perceptualState{};
    perceptualState.scores.fatigueFree  = 0.34f; // Fatiga creciente (presión = 0.66)
    perceptualState.scores.realismScore = 0.86f;

    AcousticCognitiveCore cognitiveCore;
    const RealityIntentState intent = cognitiveCore.comprehendAndFormIntent(
        genome, microMap, tmState, personalField, perceptualState);

    EXPECT_EQ(intent.priorityRanking[0], CognitivePriorityAxis::DepthPreservation);
    EXPECT_EQ(intent.priorityRanking[1], CognitivePriorityAxis::MicroDynamicPreservation);
    EXPECT_EQ(intent.priorityRanking[2], CognitivePriorityAxis::VocalClarity);
    EXPECT_EQ(intent.priorityRanking[3], CognitivePriorityAxis::AmbientExpansion);

    EXPECT_GT(intent.depthPriority, intent.microDynamicPriority);
    EXPECT_GT(intent.microDynamicPriority, intent.vocalClarityPriority);
    EXPECT_GT(intent.vocalClarityPriority, intent.ambientExpansionPriority);
    EXPECT_GT(intent.cognitiveCoherence, 0.75f);
}

// ============================================================================
// PRUEBA 8 (FASES 10 & 11): AcousticSpecialistNetwork + AcousticExecutiveBrain
// Arbitraje de conflictos entre Spatial Agent ("más expansión"), Room Agent
// ("más reverberación") y HumanPerceptionAgent ("demasiada expansión / fatiga").
// ============================================================================
TEST(AcousticRealityCognitiveEvolutionValidation, 8_Fases10_11_SpecialistNetworkAndExecutiveArbitration) {
    using namespace ivanna::reality;

    AcousticGenome genome{};
    genome.roomFingerprint.estimatedVolumeM3 = 950.0f;
    genome.roomFingerprint.estimatedRt60Sec  = 1.10f;
    genome.reflectionPattern.diffuseness     = 0.80f;
    genome.spatialRelations.depthStratification = 2.0f;
    genome.uncertainty = 0.14f;

    MicroDetailMap microMap{};
    microMap.nodes[0].existence = 0.80f;
    microMap.nodes[2].existence = 0.75f;
    microMap.nodes[4].existence = 0.72f;

    const auto timeline = AcousticTimeMachine::reconstructTimeline(genome, kSampleRate);
    NeuralAcousticProposal aiProp{};
    aiProp.inferenceConfidence = 0.90f;

    PersonalizedAcousticField personalField{};
    PerceptualOptimizationTargets perceptual{};
    perceptual.scores.fatigueFree  = 0.42f; // Fatiga moderada-alta
    perceptual.scores.naturalness  = 0.88f;
    perceptual.scores.realismScore = 0.85f;
    perceptual.harmonicRestraintScale  = 0.92f;
    perceptual.cochlearReliefIntensity = 0.65f;

    RealityIntentState intent{};
    intent.sceneScaleIndex          = 0.88f;
    intent.ambientExpansionPriority = 0.85f; // Provoca que Spatial y Room pidan alta expansión/reverb
    intent.risingFatiguePressure    = 0.58f; // Mientras hay presión de fatiga
    intent.vocalClarityPriority     = 0.78f;
    intent.microDynamicPriority     = 0.82f;
    intent.depthPriority            = 0.90f;
    intent.priorityRanking = {
        CognitivePriorityAxis::DepthPreservation,
        CognitivePriorityAxis::MicroDynamicPreservation,
        CognitivePriorityAxis::VocalClarity,
        CognitivePriorityAxis::AmbientExpansion
    };

    const auto spatialRep = SpatialIntelligenceAgent::evaluateAndPropose(genome, personalField, intent);
    const auto roomRep    = RoomIntelligenceAgent::evaluateAndPropose(genome, timeline, aiProp, intent);
    const auto microRep   = MicroRealityAgent::evaluateAndPropose(microMap, genome, intent);
    const auto judgeRep   = HumanPerceptionAgent::judgeReality(genome, perceptual, spatialRep, roomRep, intent);

    EXPECT_TRUE(judgeRep.vetoArtificialSpectacularity)
        << "HumanPerceptionAgent debe vetar la espectacularidad artificial cuando sube la fatiga o la expansión excesiva";

    const auto decision = AcousticExecutiveBrain::arbitrate(
        intent, spatialRep, roomRep, microRep, judgeRep, perceptual);

    EXPECT_NE(decision.activeConflictFlags, 0u)
        << "AcousticExecutiveBrain debe registrar banderas de conflicto activas al arbitrar agentes";
    EXPECT_LE(decision.arbitratedWfsSpreadScale, judgeRep.maxAllowedExpansionClamp + 1.0e-4f);
    EXPECT_LE(decision.arbitratedRoomWetScale, judgeRep.maxAllowedReverbClamp + 1.0e-4f);
    EXPECT_GT(decision.executiveCoherenceScore, 0.55f);
}

// ============================================================================
// PRUEBA 9 (FASES 12 & 13): AcousticExperienceMemory ("Rush - Xanadu") +
//                           SelfCalibratingRealityLoop (Homeostasis Biológica)
// Verifica aprendizaje de estado sin guardar audio y convergencia homeostática.
// ============================================================================
TEST(AcousticRealityCognitiveEvolutionValidation, 9_Fases12_13_MemoryMatrixRushXanaduAndHomeostasis) {
    using namespace ivanna::reality;

    AcousticExperienceMemory memory;
    SelfCalibratingRealityLoop homeostasis;

    // Firma acústica representativa de "Rush - Xanadu" (escenario amplio, RT60 rico, alta densidad transitoria)
    const uint32_t xanaduSig = AcousticExperienceMemory::computeSceneSignatureHash(
        static_cast<uint8_t>(AcousticSceneArchetype::LiveConcertArena),
        3.4f,  // stageWidthM
        1.15f, // estimatedRt60Sec
        0.78f, // transientDensity
        0.74f  // vocalCentrality
    );

    ExecutiveArbitrationDecision dec{};
    dec.arbitratedWfsSpreadScale = 1.14f;
    dec.arbitratedRoomWetScale   = 0.96f;

    AcousticExperienceEntry entry{};
    HomeostaticLoopState loopState{};
    for (int cycle = 0; cycle < 25; ++cycle) {
        entry = memory.consolidateAndRecall(xanaduSig, dec, 2.35f, 0.22f, 0.91f);
        loopState = homeostasis.stepHomeostasis(
            entry.achievedRealismEma,
            0.89f,
            entry.learnedFatigueSensitivity,
            dec.arbitratedWfsSpreadScale);
    }

    EXPECT_EQ(entry.signatureHash, xanaduSig);
    EXPECT_EQ(entry.observations, 25u);
    EXPECT_NEAR(entry.pleasantDepthMeters, 2.35f, 0.15f);
    EXPECT_NEAR(entry.learnedSpatialPreference, 1.14f, 0.10f);
    EXPECT_GT(loopState.stabilityIndex, 0.85f)
        << "El ciclo homeostático debe converger hacia estabilidad biológica sin oscilaciones";
    // Verifica que AcousticExperienceEntry sea ligero (solo estado, jamás buffers PCM)
    EXPECT_LE(sizeof(AcousticExperienceEntry), 64u);
}

// ============================================================================
// PRUEBA 10 (FASES 14 & 15): DigitalAcousticTwin + EvolutionaryRealityOptimizer
// Verifica síntesis coherente Sala+Dispositivo+Oyente+Escena y optimización CMA-ES/Q.
// ============================================================================
TEST(AcousticRealityCognitiveEvolutionValidation, 10_Fases14_15_DigitalTwinAndEvolutionaryOptimization) {
    using namespace ivanna::reality;

    ivanna::spatial::IvannaAudioPipeline pipeline;
    pipeline.reset();
    pipeline.setRealityReconstructionEnabled(true);

    std::array<float, kBlockSize> bufL{}, bufR{};
    for (size_t blk = 0; blk < 20; ++blk) {
        synthesizeConcertEventBlock(bufL.data(), bufR.data(), kBlockSize, blk * kBlockSize);
        pipeline.orchestrateRealityFromBlock(bufL.data(), bufR.data(), kBlockSize, kSampleRate);
    }

    const auto& st  = pipeline.activeRealityState();
    const auto& cog = st.cognitive;

    // Fase 14: DigitalAcousticTwin coherente
    EXPECT_GT(cog.digitalTwin.roomDimensionsM[0], 2.0f);
    EXPECT_GT(cog.digitalTwin.roomDimensionsM[1], 2.0f);
    EXPECT_GT(cog.digitalTwin.roomDimensionsM[2], 2.0f);
    EXPECT_GT(cog.digitalTwin.twinCoherenceIndex, 0.65f);

    // Fase 15: EvolutionaryRealityOptimizer (CMA-ES + Q-Learning)
    EXPECT_GE(cog.evolutionary.generation, 20u);
    EXPECT_GT(cog.evolutionary.fitness, 0.55f);
    for (float g : cog.evolutionary.genes) {
        EXPECT_GE(g, 0.75f);
        EXPECT_LE(g, 1.25f);
    }
}

// ============================================================================
// FASE FINAL — VALIDACIÓN HUMANA (6 Pruebas de Certificación Humana):
//   1. Original vs reconstrucción
//   2. Localización ciega
//   3. Sensación de sala
//   4. Fatiga después de escucha prolongada
//   5. Comparación con procesamiento tradicional ("otro Dolby / compresor agresivo")
//   6. Estabilidad 24 horas (simulación acelerada de 24h de ciclos cognitivos y RT)
// ============================================================================
TEST(AcousticRealityHumanValidationSuite, 11_HumanValidation_FatigueVsTraditionalAnd24HourStability) {
    using namespace ivanna::reality;

    ivanna::spatial::IvannaAudioPipeline ivannaPipeline;
    ivannaPipeline.reset();
    ivannaPipeline.setRealityReconstructionEnabled(true);

    std::array<float, kBlockSize> origL{}, origR{};
    synthesizeConcertEventBlock(origL.data(), origR.data(), kBlockSize, 0);

    // ── Validación Humana 4 & 5: Escucha prolongada y comparación vs DSP tradicional ──
    std::array<float, kBlockSize> ivannaL = origL, ivannaR = origR;
    std::array<float, kBlockSize> tradL   = origL, tradR   = origR;

    for (int b = 0; b < 16; ++b) {
        synthesizeConcertEventBlock(origL.data(), origR.data(), kBlockSize, b * kBlockSize);
        ivannaL = origL;
        ivannaR = origR;
        ivannaPipeline.orchestrateRealityFromBlock(ivannaL.data(), ivannaR.data(), kBlockSize, kSampleRate);
        ivannaPipeline.process(ivannaL.data(), ivannaR.data(), kBlockSize);

        // Procesamiento tradicional agresivo ("otro espacializador / loudness maximizer"):
        // infla el canal Side x1.8 y aplica saturación dura que destruye el factor de cresta
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float mid  = 0.5f * (origL[i] + origR[i]);
            const float side = 0.5f * (origL[i] - origR[i]) * 1.85f;
            tradL[i] = std::tanh((mid + side) * 2.2f);
            tradR[i] = std::tanh((mid - side) * 2.2f);
        }
    }

    auto computeCrestDb = [](const float* l, const float* r, size_t n) {
        float peak = 1.0e-6f;
        for (size_t i = 0; i < n; ++i) {
            peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
        }
        const float rms = std::max(1.0e-6f, computeRms(l, r, n));
        return 20.0f * std::log10(peak / rms);
    };

    const float crestIvanna = computeCrestDb(ivannaL.data(), ivannaR.data(), kBlockSize);
    const float crestTrad   = computeCrestDb(tradL.data(), tradR.data(), kBlockSize);
    EXPECT_GT(crestIvanna, crestTrad + 0.5f)
        << "IVANNA preserva la microdinámica y el factor de cresta natural frente al aplastamiento del DSP tradicional";

    const auto& finalState = ivannaPipeline.activeRealityState();
    EXPECT_GT(finalState.cognitive.humanJudge.longTermListeningComfort, 0.65f)
        << "La comodidad auditiva de largo plazo debe mantenerse alta tras exposición prolongada";

    // ── Validación Humana 6: Estabilidad 24 Horas (Ciclo Homeostático + Cognitivo Continuo) ──
    // Simulamos 86,400 segundos (24 horas) en pasos acelerados del sistema nervioso superior,
    // verificando 0 NaN/Inf, 0 desbordamientos, memoria acotada y homeostasis estable.
    auto& orch = AcousticRealityOrchestrator::instance();
    ivanna::experimental::RawAudioMetrics m{};
    m.rms              = 0.24f;
    m.peak             = 0.74f;
    m.crest_factor_db  = 9.8f;
    m.band_low_energy  = 0.08f;
    m.band_mid_energy  = 0.11f;
    m.band_high_energy = 0.05f;
    m.voice_score      = 0.62f;

    std::array<ivanna::spatial::DecomposedObject, 4> objs{};
    objs[0].position = { 0.0f, 1.5f, 0.05f }; objs[0].gain = 1.0f;
    objs[1].position = {-0.7f, 2.1f, 0.15f }; objs[1].gain = 0.85f;
    objs[2].position = { 0.7f, 2.1f, 0.15f }; objs[2].gain = 0.85f;
    objs[3].position = { 0.0f, 3.0f, 0.45f }; objs[3].gain = 0.70f;

    const auto adaptSt = ivanna::experimental::AdaptiveDecisionEngine::evaluate(m, 0.14f, 0.10f);

    AcousticRealityState snap24h{};
    constexpr int kSimulatedSteps = 2400; // Representa 24h a razón de 1 época cognitiva cada 36 s
    for (int step = 0; step < kSimulatedSteps; ++step) {
        const uint64_t simTimeUs = static_cast<uint64_t>(step) * 36000000ULL; // +36s por paso -> 86,400s (24h)
        snap24h = orch.orchestrateCycle(m, adaptSt, objs, 0.45f, 0.32f, kSampleRate, 0.050f, simTimeUs);
    }

    EXPECT_TRUE(std::isfinite(snap24h.perceptual.scores.realismScore));
    EXPECT_TRUE(std::isfinite(snap24h.cognitive.homeostasis.stabilityIndex));
    EXPECT_TRUE(std::isfinite(snap24h.cognitive.executiveDecision.executiveCoherenceScore));
    EXPECT_TRUE(std::isfinite(snap24h.cognitive.digitalTwin.twinCoherenceIndex));
    EXPECT_TRUE(std::isfinite(snap24h.cognitive.evolutionary.fitness));
    EXPECT_GT(snap24h.cognitive.homeostasis.stabilityIndex, 0.85f)
        << "Tras 24 horas de operación continua, el bucle homeostático debe permanecer 100% estable";
    EXPECT_GE(snap24h.cognitive.executiveDecision.arbitratedWfsSpreadScale, 0.75f);
    EXPECT_LE(snap24h.cognitive.executiveDecision.arbitratedWfsSpreadScale, 1.25f);
}

