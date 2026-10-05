// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA-OMEGA-SUPREME — Test Suite: Acoustic Unity Engine (v2.6.0)
//
// Validación integral de las 7 Fases de la Evolución Acústica:
//   Fase 1: Cartografía total y conocimiento cruzado entre motores.
//   Fase 2: Cambio de paradigma: organismo cooperativo vs cadena ciega.
//   Fase 3: Reinyección diferencial evolucionada (separación transitorios/ambiente).
//   Fase 4: Motor de coherencia acústica (fase, energía, correlación, tiempo).
//   Fase 5: Reconstrucción perceptual biológica (formantes, bajo en fase, aire).
//   Fase 6: Calibración autónoma de arranque (Hermite C1 zero-pop).
//   Fase 7: Optimización en tiempo real (0 malloc, 0 mutex, POD trivially copyable).
// ============================================================================

#include <gtest/gtest.h>
#include "acoustic_unity_engine.hpp"
#include <cmath>
#include <limits>
#include <vector>

using namespace ivanna::unity;

// ── FASE 1 & FASE 7: Arquitectura, Estructuras POD y Cero Asignaciones ──────
TEST(AcousticUnityEngineTest, Phase1_7_ZeroAllocationsAndPOD) {
    static_assert(std::is_trivially_copyable<AcousticUnityContext>::value,
                  "AcousticUnityContext debe ser trivially copyable para intercambio lock-free");
    static_assert(alignof(AcousticUnityContext) >= 64,
                  "AcousticUnityContext debe estar alineado a línea de caché L1 (64 bytes)");

    auto& engine = AcousticUnityEngine::instance();
    engine.reset();
    const auto ctx = engine.getContext();
    EXPECT_FLOAT_EQ(ctx.spatialBudget, 1.0f);
    EXPECT_FLOAT_EQ(ctx.harmonicBudget, 1.0f);
    EXPECT_FLOAT_EQ(ctx.msWidenerMultiplier, 1.0f);
    EXPECT_FALSE(ctx.isCalibrated);
}

// ── FASE 2: Cambio de Paradigma — Cooperación de Motores ─────────────────────
TEST(AcousticUnityEngineTest, Phase2_CooperativeArbitration_SpeechDominance) {
    auto& engine = AcousticUnityEngine::instance();
    engine.reset();

    std::vector<float> dryL(128, 0.2f);
    std::vector<float> dryR(128, 0.2f);

    // Diálogo vocal detectado (voiceScore = 0.85)
    engine.coordinateAcousticOrganism(
        dryL.data(), dryR.data(), 128,
        /*voiceScore=*/0.85f, /*tonalityHint=*/0.10f,
        /*upmixActive=*/false, /*wfsActive=*/false,
        /*rirActive=*/true, /*harmGain=*/0.5f);

    const auto ctx = engine.getContext();
    EXPECT_EQ(ctx.scene, DominantAcousticScene::DialogueVocal);
    // El RIR debe atenuarse automáticamente para no ensuciar la inteligibilidad de diálogos
    EXPECT_FLOAT_EQ(ctx.rirWetDuckFactor, 0.25f);
    // Presencia formántica vocal activada
    EXPECT_GT(ctx.vocalFormantBoostDb, 1.5f);
    // Sub-graves retumbantes apagados en voz pura
    EXPECT_FLOAT_EQ(ctx.subHarmonicWeight, 0.0f);
    // Ensanchador enfocado hacia el centro para diálogos
    EXPECT_LE(ctx.msWidenerMultiplier, 1.05f);
}

TEST(AcousticUnityEngineTest, Phase2_CooperativeArbitration_MultichannelVsWidener) {
    auto& engine = AcousticUnityEngine::instance();
    engine.reset();

    std::vector<float> dryL(128, 0.3f);
    std::vector<float> dryR(128, 0.3f);

    // HOA Upmixing o WFS activo
    engine.coordinateAcousticOrganism(
        dryL.data(), dryR.data(), 128,
        /*voiceScore=*/0.10f, /*tonalityHint=*/0.20f,
        /*upmixActive=*/true, /*wfsActive=*/false,
        /*rirActive=*/false, /*harmGain=*/0.8f);

    const auto ctx = engine.getContext();
    EXPECT_EQ(ctx.scene, DominantAcousticScene::CinematicMultichannel);
    // El ensanchador M/S lateral DEBE ser 1.0 (neutro) para no romper la física de fase del HOA
    EXPECT_FLOAT_EQ(ctx.msWidenerMultiplier, 1.0f);
    EXPECT_GT(ctx.wfsSpreadMultiplier, 0.8f);
}

// ── FASE 3: Reinyección Diferencial Evolucionada ─────────────────────────────
TEST(AcousticUnityEngineTest, Phase3_EvolvedDifferential_TransientAttackMasking) {
    float stateFast = 0.0f;
    float stateSlow = 0.0f;

    // 1. Simulación de ataque percusivo agudo (redoblante o consonante oclusiva)
    float attackMask = EvolvedDifferentialSeparator::updateTransientDetector(0.95f, stateFast, stateSlow);
    EXPECT_GT(attackMask, 0.40f); // Detectó transitorio rápido

    float outL = 0.0f, outR = 0.0f;
    const float dryL = 0.80f, dryR = 0.80f;
    const float deltaL = 0.30f, deltaR = 0.30f;

    EvolvedDifferentialSeparator::synthesizeUnityDifferential(
        dryL, dryR, deltaL, deltaR, attackMask,
        /*immersionGain=*/0.8f, /*spatialBudget=*/1.0f,
        outL, outR);

    // El ataque seco debe preservarse con mínima fuga de retardo (para evitar filtro de peine)
    const float rawSum = dryL + 0.8f * deltaL;
    EXPECT_LT(outL, rawSum); // Atenuó la fuga del delta en el ataque
    EXPECT_GT(outL, dryL);   // Mantiene el nivel base

    // 2. Simulación de tono sostenido estacionario (sustain/reverb ~25 ms @ 48kHz)
    float maskSustain = 0.0f;
    for (int i = 0; i < 1200; ++i) {
        maskSustain = EvolvedDifferentialSeparator::updateTransientDetector(0.50f, stateFast, stateSlow);
    }
    EXPECT_LT(maskSustain, 0.05f); // No hay ataque: es sustain estacionario

    float outSustainL = 0.0f, outSustainR = 0.0f;
    EvolvedDifferentialSeparator::synthesizeUnityDifferential(
        dryL, dryR, deltaL, deltaR, maskSustain,
        /*immersionGain=*/0.8f, /*spatialBudget=*/1.0f,
        outSustainL, outSustainR);

    // En el sustain/reverb, la inmersión espacial pasa completa
    EXPECT_NEAR(outSustainL, dryL + 0.8f * deltaL, 0.05f);
}

// ── FASE 4: Guardián de Coherencia Acústica ──────────────────────────────────
TEST(AcousticUnityEngineTest, Phase4_CoherenceGuard_AntiPhaseProtection) {
    constexpr size_t N = 128;
    std::vector<float> l(N), rInPhase(N), rAntiPhase(N);

    for (size_t i = 0; i < N; ++i) {
        const float s = std::sin(2.0f * M_PI * i / 16.0f);
        l[i] = s;
        rInPhase[i] = s;
        rAntiPhase[i] = -s; // 180 grados fuera de fase
    }

    const float corrIn = AcousticCoherenceGuard::evaluateInterauralPhaseCorrelation(l.data(), rInPhase.data(), N);
    const float corrAnti = AcousticCoherenceGuard::evaluateInterauralPhaseCorrelation(l.data(), rAntiPhase.data(), N);

    EXPECT_NEAR(corrIn, 1.0f, 1e-3f);
    EXPECT_NEAR(corrAnti, -1.0f, 1e-3f);

    auto& engine = AcousticUnityEngine::instance();
    engine.reset();

    // Alimentar señal fuera de fase
    engine.coordinateAcousticOrganism(
        l.data(), rAntiPhase.data(), N,
        /*voiceScore=*/0.0f, /*tonalityHint=*/0.5f,
        /*upmixActive=*/false, /*wfsActive=*/false,
        /*rirActive=*/false, /*harmGain=*/0.5f);

    const auto ctx = engine.getContext();
    // Ante una señal con riesgo severo de cancelación mono, el ensanchador no debe inflar más los laterales
    EXPECT_LE(ctx.msWidenerMultiplier, 1.05f);
}

TEST(AcousticUnityEngineTest, Phase4_CoherenceGuard_EnergyCeiling) {
    constexpr size_t N = 64;
    std::vector<float> l(N, 1.50f);
    std::vector<float> r(N, 1.40f);

    // Etapa con sobreganancia
    AcousticCoherenceGuard::enforceStageEnergyCeiling(l.data(), r.data(), N, 1.15f);

    for (size_t i = 0; i < N; ++i) {
        EXPECT_LE(std::fabs(l[i]), 1.1501f);
        EXPECT_LE(std::fabs(r[i]), 1.1501f);
    }
}

// ── FASE 5: Reconstrucción Perceptual (Música Audiófila) ──────────────────────
TEST(AcousticUnityEngineTest, Phase5_PerceptualReconstruction_MusicScene) {
    auto& engine = AcousticUnityEngine::instance();
    engine.reset();

    std::vector<float> dryL(128, 0.25f);
    std::vector<float> dryR(128, 0.25f);

    // Música tonal audiófila detectada
    engine.coordinateAcousticOrganism(
        dryL.data(), dryR.data(), 128,
        /*voiceScore=*/0.10f, /*tonalityHint=*/0.85f,
        /*upmixActive=*/false, /*wfsActive=*/false,
        /*rirActive=*/true, /*harmGain=*/0.4f);

    const auto ctx = engine.getContext();
    EXPECT_EQ(ctx.scene, DominantAcousticScene::AudiophileMusic);
    // Reconstrucción de aire en altas frecuencias
    EXPECT_GT(ctx.highAirTiltDb, 0.5f);
    // Refuerzo de peso táctil de graves en fase
    EXPECT_GT(ctx.subHarmonicWeight, 0.25f);
    // Sala natural preservada al 100%
    EXPECT_FLOAT_EQ(ctx.rirWetDuckFactor, 1.0f);
}

// ── FASE 6: Calibración Autónoma en Frío (Hermite C1 Zero-Pop) ───────────────
TEST(AcousticUnityEngineTest, Phase6_AutonomousBootCalibrator_ZeroPopCurve) {
    AutonomousBootCalibrator calibrator;
    EXPECT_FALSE(calibrator.isCalibrated());

    // Bloque 0: ganancia 0.0 (cero DC pop en conexión)
    EXPECT_FLOAT_EQ(calibrator.getColdStartGain(0), 0.0f);

    // Bloques intermedios: rampa Hermite suave C1 (3t^2 - 2t^3)
    const float gMid = calibrator.getColdStartGain(4);
    EXPECT_NEAR(gMid, 0.50f, 0.01f);

    // Registrar los 8 bloques de warmup
    for (size_t b = 0; b < AutonomousBootCalibrator::kWarmupBlocks; ++b) {
        calibrator.registerBootBlock(0.15f);
    }

    EXPECT_TRUE(calibrator.isCalibrated());
    EXPECT_FLOAT_EQ(calibrator.getColdStartGain(8), 1.0f);
    EXPECT_FLOAT_EQ(calibrator.getColdStartGain(100), 1.0f);
}
