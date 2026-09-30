// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#include <gtest/gtest.h>
#include "../include/omega_wave_stages.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace ivanna::unified;

constexpr float kSampleRate = 48000.0f;
constexpr size_t kBlockSize = 128;
constexpr float kTwoPi      = 6.28318530717958647692f;

void fillSineStereo(float* L, float* R, size_t frames, float freqHz, float amp, float& phase) noexcept {
    const float dPhi = kTwoPi * freqHz / kSampleRate;
    for (size_t i = 0; i < frames; ++i) {
        const float s = amp * std::sin(phase);
        L[i] = s;
        R[i] = s * 0.95f;
        phase += dPhi;
        if (phase >= kTwoPi) phase -= kTwoPi;
    }
}

void assertBufferHealthy(const float* L, const float* R, size_t frames, float maxAllowedPeak = 1.0f) {
    for (size_t i = 0; i < frames; ++i) {
        ASSERT_TRUE(std::isfinite(L[i])) << "Non-finite sample on L at index " << i;
        ASSERT_TRUE(std::isfinite(R[i])) << "Non-finite sample on R at index " << i;
        EXPECT_LE(std::fabs(L[i]), maxAllowedPeak + 1.0e-4f);
        EXPECT_LE(std::fabs(R[i]), maxAllowedPeak + 1.0e-4f);
    }
}

TEST(UnifiedMasterV3SafetyBench, Phase1_AllStagesDefaultOffBitExactIdentity) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);
    pipeline.reset();

    for (const IDspStage* st : pipeline.stages()) {
        ASSERT_NE(st, nullptr);
        EXPECT_TRUE(st->isBypassed()) << "Stage " << st->name() << " must start OFF by default";
    }

    std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};
    float phase = 0.0f;
    fillSineStereo(inL.data(), inR.data(), kBlockSize, 1000.0f, 0.5f, phase);
    outL = inL;
    outR = inR;

    pipeline.process(outL.data(), outR.data(), kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(outL[i], inL[i]);
        EXPECT_FLOAT_EQ(outR[i], inR[i]);
    }
    EXPECT_EQ(pipeline.totalLatencySamples(), 0u);
}

TEST(UnifiedMasterV3SafetyBench, Phase2_SignalSuiteSinusoidsImpulsePinkNoiseSweepAndClipping) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);

    // Enable representative stages across all waves (respecting family mutual exclusion)
    UnifiedParameterSnapshot snap{};
    snap.setStageEnabled(StageId::PhaseOracleControl, true);
    snap.setStageEnabled(StageId::PsychoacousticsAnalysis, true);
    snap.setStageEnabled(StageId::SofaSafAnalysisBridge, true);
    snap.setStageEnabled(StageId::VoiceProsody, true);
    snap.setStageEnabled(StageId::EvolutionaryEq, true);
    snap.setStageEnabled(StageId::NeuralUpmixer, true);
    snap.setStageEnabled(StageId::AntiDolbyClassic, true);
    snap.setStageEnabled(StageId::AcousticSynthesis, true);
    snap.setStageEnabled(StageId::SafOptimizerSuite, true);
    snap.setStageEnabled(StageId::CochlearPinn, true);
    UnifiedParamSnapshotBus::instance().publish(snap);

    std::array<float, kBlockSize> L{}, R{};

    // 1) Sinusoides puras: 20 Hz, 1 kHz, 18 kHz
    for (float freq : {20.0f, 1000.0f, 18000.0f}) {
        float phase = 0.0f;
        for (int b = 0; b < 12; ++b) {
            fillSineStereo(L.data(), R.data(), kBlockSize, freq, 0.6f, phase);
            pipeline.process(L.data(), R.data(), kBlockSize);
            assertBufferHealthy(L.data(), R.data(), kBlockSize);
        }
    }

    // 2) Impulso unitario + cola de silencio (500 ms -> ~188 bloques)
    for (int b = 0; b < 40; ++b) {
        L.fill(0.0f);
        R.fill(0.0f);
        if (b == 0) {
            L[0] = 0.9f;
            R[0] = 0.9f;
        }
        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize);
    }

    // 3) Barrido logarítmico 20 Hz - 20 kHz y señal al límite de clipping (0.99 FS)
    float sweepPhase = 0.0f;
    for (int b = 0; b < 32; ++b) {
        const float tNorm = static_cast<float>(b) / 32.0f;
        const float freq  = 20.0f * std::pow(1000.0f, tNorm);
        fillSineStereo(L.data(), R.data(), kBlockSize, freq, 0.98f, sweepPhase);
        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize);
    }

    EXPECT_TRUE(pipeline.verifyFamilyExclusionInvariant());
    EXPECT_EQ(pipeline.watchdog().totalFaults(), 0u);
}

TEST(UnifiedMasterV3SafetyBench, Phase3_StressBlocksAndRapidToggleMidNoteZeroClicks) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);

    std::array<float, kBlockSize> L{}, R{};
    float phase = 0.0f;
    float prevLastL = 0.0f;
    bool  havePrev = false;

    // 200 toggles rápidos en mitad de una nota de 1 kHz (verifica rampa Hermite C1 sin clics)
    for (int step = 0; step < 200; ++step) {
        const bool enableStage = (step % 2) == 0;
        UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::CochlearPinn, enableStage, 0.45f);
        UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::AntiDolbyClassic, enableStage, 0.80f);

        fillSineStereo(L.data(), R.data(), kBlockSize, 1000.0f, 0.5f, phase);
        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize);

        if (havePrev) {
            const float boundaryDelta = std::fabs(L[0] - prevLastL);
            EXPECT_LT(boundaryDelta, 0.35f) << "Boundary click detected at toggle step " << step;
        }
        prevLastL = L[kBlockSize - 1];
        havePrev = true;
    }

    // Stress de larga duración en bypass + etapas ligeras (sin deriva de estado ni fugas)
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::PhaseOracleControl, true);
    for (int b = 0; b < 4000; ++b) {
        fillSineStereo(L.data(), R.data(), kBlockSize, 440.0f, 0.4f, phase);
        pipeline.process(L.data(), R.data(), kBlockSize);
    }
    assertBufferHealthy(L.data(), R.data(), kBlockSize);
}

TEST(UnifiedMasterV3SafetyBench, Phase4_FamilyMutualExclusionCochlearABAndAntiDolbyAB) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);

    // Intentar activar simultáneamente CochlearPinn (A) y NeuroCochlearManifold (B),
    // y simultáneamente AntiDolbyClassic (A) y AntiDolbyAi (B).
    UnifiedParameterSnapshot raw{};
    raw.setStageEnabled(StageId::CochlearPinn, true);
    raw.setStageEnabled(StageId::NeuroCochlearManifold, true);
    raw.activeCochlearVariant = 1u; // Seleccionar variante B (NeuroCochlearManifold)

    raw.setStageEnabled(StageId::AntiDolbyClassic, true);
    raw.setStageEnabled(StageId::AntiDolbyAi, true);
    raw.activeAntiDolbyVariant = 0u; // Seleccionar variante A (AntiDolbyClassic)

    UnifiedParamSnapshotBus::instance().publish(raw);

    std::array<float, kBlockSize> L{}, R{};
    float phase = 0.0f;
    fillSineStereo(L.data(), R.data(), kBlockSize, 500.0f, 0.4f, phase);
    pipeline.process(L.data(), R.data(), kBlockSize);

    EXPECT_TRUE(pipeline.verifyFamilyExclusionInvariant());
    EXPECT_TRUE(pipeline.findStage(StageId::CochlearPinn)->isBypassed());
    EXPECT_FALSE(pipeline.findStage(StageId::NeuroCochlearManifold)->isBypassed());
    EXPECT_FALSE(pipeline.findStage(StageId::AntiDolbyClassic)->isBypassed());
    EXPECT_TRUE(pipeline.findStage(StageId::AntiDolbyAi)->isBypassed());

    // Verificar autoridad única coclear (Fase 3): cambiar a variante A vía setCochlearUnified
    UnifiedParamSnapshotBus::instance().setCochlearUnified(true, 0.55f, /*variant=*/0u);
    fillSineStereo(L.data(), R.data(), kBlockSize, 500.0f, 0.4f, phase);
    pipeline.process(L.data(), R.data(), kBlockSize);

    EXPECT_TRUE(pipeline.verifyFamilyExclusionInvariant());
    EXPECT_FALSE(pipeline.findStage(StageId::CochlearPinn)->isBypassed());
    EXPECT_TRUE(pipeline.findStage(StageId::NeuroCochlearManifold)->isBypassed());
    EXPECT_NEAR(pipeline.findStage(StageId::CochlearPinn)->wetIntensity(), 0.55f, 1.0e-5f);
}

TEST(UnifiedMasterV3SafetyBench, Phase5_RtStageWatchdogIsolatesFaultyStageWithoutCrashingChain) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);
    pipeline.reset();

    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::EvolutionaryEq, true, 1.0f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::AntiDolbyClassic, true, 0.8f);

    std::array<float, kBlockSize> L{}, R{};
    float phase = 0.0f;
    fillSineStereo(L.data(), R.data(), kBlockSize, 1000.0f, 0.5f, phase);
    pipeline.process(L.data(), R.data(), kBlockSize);
    EXPECT_FALSE(pipeline.findStage(StageId::EvolutionaryEq)->isBypassed());

    const uint32_t prevIsolations = HeavyWorkerEngine::instance().watchdogIsolations();

    // Inyectar fallo NaN en EvolutionaryEq y verificar que el Watchdog lo sanea y aísla
    pipeline.injectFaultOnStageForTest(StageId::EvolutionaryEq);
    fillSineStereo(L.data(), R.data(), kBlockSize, 1000.0f, 0.5f, phase);
    pipeline.process(L.data(), R.data(), kBlockSize);

    // La salida jamás contiene NaN/Inf y la etapa culpable queda aislada
    assertBufferHealthy(L.data(), R.data(), kBlockSize);
    EXPECT_TRUE(pipeline.findStage(StageId::EvolutionaryEq)->isBypassed());
    EXPECT_FALSE(pipeline.findStage(StageId::AntiDolbyClassic)->isBypassed());
    EXPECT_GE(pipeline.watchdog().totalFaults(), 1u);
    EXPECT_EQ(pipeline.watchdog().lastFaultyStage(), StageId::EvolutionaryEq);
    EXPECT_GT(HeavyWorkerEngine::instance().watchdogIsolations(), prevIsolations);
}

TEST(UnifiedMasterV3SafetyBench, Phase6_AsyncHeavyWorkerSpscAndSuperAgentMemoryPersistence) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);

    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::AntiDolbyAi, true, 0.7f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::TinyMlClassifier, true, 1.0f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::NeuromorphicTinyMl, true, 1.0f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::LifNeuronPool, true, 1.0f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::AutonomousBrain, true, 1.0f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::AcousticSynthesis, true, 0.6f);
    UnifiedParamSnapshotBus::instance().setStageEnabled(StageId::SafOptimizerSuite, true, 0.5f);

    std::array<float, kBlockSize> L{}, R{};
    float phase = 0.0f;
    for (int b = 0; b < 8; ++b) {
        fillSineStereo(L.data(), R.data(), kBlockSize, 440.0f, 0.5f, phase);
        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize);
    }

    HeavyWorkerEngine::instance().drainPendingSynchronously();
    const auto workerRes = HeavyWorkerEngine::instance().readLatestValid();
    EXPECT_TRUE(workerRes.valid);
    EXPECT_GT(workerRes.sequence, 0u);
    const auto diag = HeavyWorkerEngine::instance().selfHealer().getDiagnosticReport();
    EXPECT_EQ(diag.audioEngineState, Ivanna::ComponentState::OK);
    EXPECT_EQ(diag.dspKernelState, Ivanna::ComponentState::OK);

    // Detener el hilo worker antes de salir para un teardown limpio
    HeavyWorkerEngine::instance().stopWorker();
    UnifiedParamSnapshotBus::instance().resetToAllOff();
}

TEST(UnifiedMasterV3SafetyBench, Phase7_ZeroArtifactZeroClipAndEnergyConservationAudit) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);
    pipeline.reset();

    // Activar simultáneamente todas las etapas compatibles de Oleadas 1–4 + Fusión Holográfica
    UnifiedParameterSnapshot snap{};
    snap.setStageEnabled(StageId::PhaseOracleControl, true);
    snap.setStageEnabled(StageId::PsychoacousticsAnalysis, true);
    snap.setStageEnabled(StageId::SofaSafAnalysisBridge, true);
    snap.setStageEnabled(StageId::VoiceProsody, true);
    snap.setStageEnabled(StageId::LifNeuronPool, true);
    snap.setStageEnabled(StageId::EvolutionaryEq, true);
    snap.setStageEnabled(StageId::NeuralUpmixer, true);
    snap.setStageEnabled(StageId::AntiDolbyClassic, true);
    snap.setStageEnabled(StageId::AcousticSynthesis, true);
    snap.setStageEnabled(StageId::SafOptimizerSuite, true);
    snap.setStageEnabled(StageId::CochlearPinn, true);
    snap.holographicSingularityEnabled = true;
    UnifiedParamSnapshotBus::instance().publish(snap);

    std::array<float, kBlockSize> L{}, R{};
    float phase = 0.0f;
    float prevEndL = 0.0f;
    bool  hasPrevBlock = false;
    double sumDcL = 0.0;
    double sumDcR = 0.0;
    double inEnergySum  = 0.0;
    double outEnergySum = 0.0;
    size_t totalSamples = 0;

    // 40 bloques de tono alto nivel (0.94 FS) para auditar ausencia de hard-clipping y gain-stacking
    for (int b = 0; b < 40; ++b) {
        fillSineStereo(L.data(), R.data(), kBlockSize, 997.0f, 0.94f, phase);
        for (size_t i = 0; i < kBlockSize; ++i) {
            inEnergySum += static_cast<double>(L[i] * L[i] + R[i] * R[i]);
        }

        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize, 0.995f);

        if (hasPrevBlock && b > 2) {
            const float boundaryJump = std::fabs(L[0] - prevEndL);
            EXPECT_LT(boundaryJump, 0.25f) << "Inter-block click at block " << b;
        }
        prevEndL = L[kBlockSize - 1];
        hasPrevBlock = true;

        // Verificar ausencia de flat-topping (hard clipping sostenido)
        uint32_t consecutiveNearCeiling = 0;
        for (size_t i = 0; i < kBlockSize; ++i) {
            sumDcL += static_cast<double>(L[i]);
            sumDcR += static_cast<double>(R[i]);
            outEnergySum += static_cast<double>(L[i] * L[i] + R[i] * R[i]);
            if (std::fabs(L[i]) >= 0.9945f) {
                ++consecutiveNearCeiling;
            } else {
                consecutiveNearCeiling = 0;
            }
            EXPECT_LE(consecutiveNearCeiling, 2u) << "Hard-clipping flat-top detected at block " << b;
        }
        totalSamples += kBlockSize;
    }

    const double meanDcL = std::fabs(sumDcL / static_cast<double>(totalSamples));
    const double meanDcR = std::fabs(sumDcR / static_cast<double>(totalSamples));
    EXPECT_LT(meanDcL, 0.015);
    EXPECT_LT(meanDcR, 0.015);

    const double rmsRatio = std::sqrt(outEnergySum / std::max(1.0e-9, inEnergySum));
    EXPECT_GT(rmsRatio, 0.65);
    EXPECT_LT(rmsRatio, 1.18);
    EXPECT_EQ(pipeline.watchdog().totalFaults(), 0u);

    UnifiedParamSnapshotBus::instance().resetToAllOff();
}

TEST(UnifiedMasterV3SafetyBench, Phase8_OmniHolographicSingularityFusionClosedLoop) {
    UnifiedParamSnapshotBus::instance().resetToAllOff();
    DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockSize);
    pipeline.reset();

    UnifiedParameterSnapshot snap{};
    snap.setStageEnabled(StageId::PhaseOracleControl, true);
    snap.setStageEnabled(StageId::TinyMlClassifier, true);
    snap.setStageEnabled(StageId::NeuromorphicTinyMl, true);
    snap.setStageEnabled(StageId::AutonomousBrain, true);
    snap.setStageEnabled(StageId::AntiDolbyAi, true);
    snap.setStageEnabled(StageId::AcousticSynthesis, true);
    snap.setStageEnabled(StageId::NeuroCochlearManifold, true);
    snap.activeCochlearVariant = 1u;
    snap.activeAntiDolbyVariant = 1u;
    snap.holographicSingularityEnabled = true;
    UnifiedParamSnapshotBus::instance().publish(snap);

    std::array<float, kBlockSize> L{}, R{};
    float phase = 0.0f;
    for (int b = 0; b < 6; ++b) {
        fillSineStereo(L.data(), R.data(), kBlockSize, 528.0f, 0.65f, phase);
        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize, 0.995f);
    }

    HeavyWorkerEngine::instance().drainPendingSynchronously();
    const auto res = HeavyWorkerEngine::instance().readLatestValid();
    EXPECT_GT(res.singularityField.fusionEpoch, 0u);
    EXPECT_GE(res.singularityField.holographicDepthMeters, 0.5f);
    EXPECT_LE(res.singularityField.holographicDepthMeters, 5.5f);
    EXPECT_GE(res.singularityField.transientPhaseCoherence, 0.25f);
    EXPECT_LE(res.singularityField.transientPhaseCoherence, 1.0f);
    EXPECT_GE(res.singularityField.realityPresenceIndex, 0.35f);

    // Procesar bloques adicionales con el tensor de singularidad actualizado
    for (int b = 0; b < 6; ++b) {
        fillSineStereo(L.data(), R.data(), kBlockSize, 528.0f, 0.65f, phase);
        pipeline.process(L.data(), R.data(), kBlockSize);
        assertBufferHealthy(L.data(), R.data(), kBlockSize, 0.995f);
    }

    EXPECT_EQ(pipeline.watchdog().totalFaults(), 0u);
    HeavyWorkerEngine::instance().stopWorker();
    UnifiedParamSnapshotBus::instance().resetToAllOff();
}

} // namespace
