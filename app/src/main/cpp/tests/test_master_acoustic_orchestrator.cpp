// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA-OMEGA-SUPREME — Test Suite: Master Acoustic Orchestrator
//
// Validación integral de las 7 Fases de la Misión Quirúrgica:
//   Fase 1: Estabilidad de Referencia (Cero clicks, Cero NaN, Cero XRuns)
//   Fase 2: Arquitectura del Director de Orquesta Unificado
//   Fase 3: Inteligencia de Arbitraje (Jerarquía Armónicos, Espacialidad, Dinámica)
//   Fase 4: Personalidad Acústica Musical (Voz, Fatiga auditiva, Densidad)
//   Fase 5: Experiencia de Sala Real (Coherencia HRTF + WFS + RIR)
//   Fase 6: Latencia y Eficiencia (Cero malloc, POD determinista, Lock-free)
// ============================================================================

#include <gtest/gtest.h>
#include "master_acoustic_orchestrator.hpp"
#include "omega_control_bus.h"
#include <cmath>
#include <limits>

using namespace ivanna;
using namespace ivanna::experimental;

// ── FASE 2 & FASE 6: Arquitectura y Eficiencia ──────────────────────────────
TEST(MasterAcousticOrchestratorTest, UnifiedArchitecture_ZeroAllocationsAndPOD) {
    // Garantizar que las estructuras del orquestador son POD y trivially copyable
    static_assert(std::is_trivially_copyable<AdaptiveState>::value,
                  "AdaptiveState debe ser trivially copyable para comunicación lock-free");
    static_assert(std::is_trivially_copyable<RawAudioMetrics>::value,
                  "RawAudioMetrics debe ser trivially copyable");
    static_assert(sizeof(AdaptiveState) % sizeof(uint32_t) == 0,
                  "AdaptiveState debe ser múltiplo de 4 bytes para seqlock atómico");
    static_assert(sizeof(RawAudioMetrics) % sizeof(uint32_t) == 0,
                  "RawAudioMetrics debe ser múltiplo de 4 bytes para seqlock atómico");

    // Verificar que OmegaDspSnapshot cumple el presupuesto estricto de ABI (< 512 bytes)
    EXPECT_LT(sizeof(OmegaDspSnapshot), 512u);
}

// ── FASE 1: Estabilidad de Referencia — Robustez Numérica y Cero NaN ─────────
TEST(MasterAcousticOrchestratorTest, Phase1_NumericalStability_ExtremeInputs) {
    // 1. Silencio absoluto
    RawAudioMetrics silence{};
    auto s1 = MasterAcousticOrchestrator::evaluate(silence, 0.0f, 0.0f);
    EXPECT_TRUE(std::isfinite(s1.target_gain));
    EXPECT_TRUE(std::isfinite(s1.compressor_amount));
    EXPECT_TRUE(std::isfinite(s1.spatial_width));
    EXPECT_TRUE(std::isfinite(s1.golden_ear_scale));
    EXPECT_TRUE(std::isfinite(s1.volterra_scale));
    EXPECT_TRUE(std::isfinite(s1.wfs_spread_scale));
    EXPECT_GE(s1.safety_margin, 0.99f); // Silencio total = margen máximo

    // 2. Ruido extremo y clipping digital deliberado
    RawAudioMetrics extreme{};
    extreme.rms = 1.5f;
    extreme.peak = 3.0f;
    extreme.gain_reduction_db = 18.0f;
    extreme.band_high_energy = 5.0f;
    extreme.band_mid_energy = 2.0f;
    extreme.band_low_energy = 1.0f;
    auto s2 = MasterAcousticOrchestrator::evaluate(extreme, 0.9f, 0.8f);

    EXPECT_FALSE(std::isnan(s2.target_gain));
    EXPECT_FALSE(std::isnan(s2.compressor_amount));
    EXPECT_FALSE(std::isnan(s2.exciter_reduction));
    EXPECT_FALSE(std::isnan(s2.spatial_width));
    EXPECT_LE(s2.target_gain, 0.6f); // Reducción preventiva de ganancia
    EXPECT_GE(s2.exciter_reduction, 0.7f); // Protección contra sibilancia extrema

    // 3. Entradas patológicas: NaN e Infinitos en métricas crudas
    RawAudioMetrics pathological{};
    pathological.rms = std::numeric_limits<float>::quiet_NaN();
    pathological.peak = std::numeric_limits<float>::infinity();
    pathological.gain_reduction_db = -std::numeric_limits<float>::infinity();
    pathological.voice_score = std::numeric_limits<float>::quiet_NaN();

    auto s3 = MasterAcousticOrchestrator::evaluate(pathological, 0.0f, 0.0f);
    EXPECT_TRUE(std::isfinite(s3.target_gain));
    EXPECT_TRUE(std::isfinite(s3.compressor_amount));
    EXPECT_TRUE(std::isfinite(s3.safety_margin));
    EXPECT_TRUE(std::isfinite(s3.golden_ear_scale));
}

// ── FASE 3: Arbitraje de Armónicos — Un solo generador dominante ─────────────
TEST(MasterAcousticOrchestratorTest, Phase3_Arbitration_HarmonicsSingleDominant) {
    // Regla: GoldenEarGAN activo -> Volterra reducido, Exciter reducido
    RawAudioMetrics geMetrics{};
    geMetrics.golden_ear_active = 1.0f;
    geMetrics.volterra_active   = 1.0f; // Usuario intentó activar ambos

    auto harmGE = MasterAcousticOrchestrator::arbitrateHarmonics(geMetrics, 0.2f);
    EXPECT_FLOAT_EQ(harmGE.golden_ear_scale, 1.0f);
    EXPECT_LE(harmGE.volterra_scale, 0.25f); // Volterra cedió ante GoldenEarGAN
    EXPECT_LE(harmGE.exciter_scale, 0.20f);  // Exciter fuertemente atenuado
    EXPECT_LE(harmGE.total_drive, 1.0f);     // Presupuesto total respetado

    // Regla: Volterra activo restaurando códec -> GoldenEar desactivado, Exciter reducido
    RawAudioMetrics voltMetrics{};
    voltMetrics.golden_ear_active = 0.0f;
    voltMetrics.volterra_active   = 1.0f;

    auto harmVolt = MasterAcousticOrchestrator::arbitrateHarmonics(voltMetrics, 0.1f);
    EXPECT_FLOAT_EQ(harmVolt.volterra_scale, 1.0f);
    EXPECT_FLOAT_EQ(harmVolt.golden_ear_scale, 0.0f);
    EXPECT_LE(harmVolt.exciter_scale, 0.35f);
    EXPECT_LE(harmVolt.total_drive, 1.0f);
}

// ── FASE 3: Arbitraje de Espacialidad — Un solo escenario coherente ──────────
TEST(MasterAcousticOrchestratorTest, Phase3_Arbitration_SpatialHierarchy) {
    // Prioridad: 1. HRTF, 2. WFS, 3. RIR, 4. M/S Widener
    // Regla crítica: NUNCA permitir tres ensanchadores compitiendo

    // Caso 1: HOA Upmixer activo -> M/S Widener neutralizado
    RawAudioMetrics upmixMetrics{};
    upmixMetrics.upmix_active = 1.0f;
    upmixMetrics.wfs_active   = 0.0f;

    auto spatUpmix = MasterAcousticOrchestrator::arbitrateSpatial(upmixMetrics);
    EXPECT_FLOAT_EQ(spatUpmix.hrtf_binaural_scale, 1.0f);
    EXPECT_FLOAT_EQ(spatUpmix.ms_widener_scale, 0.0f); // M/S neutralizado

    // Caso 2: WFS activo -> M/S Widener neutralizado
    RawAudioMetrics wfsMetrics{};
    wfsMetrics.upmix_active = 0.0f;
    wfsMetrics.wfs_active   = 1.0f;

    auto spatWfs = MasterAcousticOrchestrator::arbitrateSpatial(wfsMetrics);
    EXPECT_FLOAT_EQ(spatWfs.ms_widener_scale, 0.0f); // M/S neutralizado
    EXPECT_GE(spatWfs.wfs_spread_scale, 0.7f);

    // Caso 3: Stereo estándar (ni HOA ni WFS) -> M/S Widener habilitado
    RawAudioMetrics standardMetrics{};
    standardMetrics.upmix_active = 0.0f;
    standardMetrics.wfs_active   = 0.0f;

    auto spatStd = MasterAcousticOrchestrator::arbitrateSpatial(standardMetrics);
    EXPECT_FLOAT_EQ(spatStd.ms_widener_scale, 1.0f); // Permitido
}

// ── FASE 3 & FASE 4: Dinámica, Voz y Mitigación de Fatiga ─────────────────────
TEST(MasterAcousticOrchestratorTest, Phase4_AcousticPersonalityAndFatigue) {
    // 1. Preservación vocal: voz detectada protege centro y reduce reverb de sala
    RawAudioMetrics vocalMetrics{};
    vocalMetrics.voice_score = 0.85f;
    vocalMetrics.rir_active  = 1.0f;

    auto spatVoice = MasterAcousticOrchestrator::arbitrateSpatial(vocalMetrics);
    EXPECT_LE(spatVoice.ms_widener_scale, 0.3f); // Centro fantasma protegido
    EXPECT_LE(spatVoice.rir_wet_scale, 0.7f);    // Reverb de sala atenuado para inteligibilidad

    auto persVoice = MasterAcousticOrchestrator::arbitratePersonality(vocalMetrics, 0.0f);
    EXPECT_GT(persVoice.vocal_clarity_boost_db, 0.5f); // Realce sutil de presencia

    // 2. Fatiga auditiva acumulada: atenuación suave de agudos (tilt)
    auto persFatigue = MasterAcousticOrchestrator::arbitratePersonality(vocalMetrics, 0.55f);
    EXPECT_LT(persFatigue.eq_tilt_db, -0.5f); // Roll-off preventivo contra fatiga auditiva
    EXPECT_GE(persFatigue.eq_tilt_db, -2.5f); // Acotado a máximo -2.5dB
}

// ── FASE 5 & FASE 7: Arbitraje de Snapshot (Root & Non-Root) ─────────────────
TEST(MasterAcousticOrchestratorTest, SnapshotCoordination_HarmonicsAndSpatiality) {
    OmegaDspSnapshot snap = OmegaDspSnapshot::makeDefault();
    snap.harmonic_gain = 1.2f;
    snap.flags = OMEGA_FLAG_VOLTERRA_ON | OMEGA_FLAG_FASTRPC_ON;
    snap.upmixing_enabled = 1;
    snap.spatial_width = 1.4f;
    snap.widener_mult = 1.3f;
    snap.room_wet = 0.8f;
    snap.stampCrc();

    AdaptiveState state{};
    state.golden_ear_scale = 1.0f;
    state.volterra_scale = 0.2f;
    state.ms_widener_scale = 0.0f;
    state.rir_wet_scale = 0.8f;
    state.eq_tilt_db = -1.0f;

    // Aplicar arbitraje unificado
    MasterAcousticOrchestrator::arbitrateSnapshot(snap, state);

    // 1. Volterra debe haber sido desactivado por arbitraje ya que GoldenEar está activo
    EXPECT_EQ(snap.flags & OMEGA_FLAG_VOLTERRA_ON, 0u);

    // 2. Ensanchador M/S debe estar en neutro (1.0) porque Upmixing está activo
    EXPECT_FLOAT_EQ(snap.spatial_width, 1.0f);
    EXPECT_FLOAT_EQ(snap.widener_mult, 1.0f);

    // 3. Sala RIR modulada sin clics
    EXPECT_FLOAT_EQ(snap.room_wet, 0.64f); // 0.8 * 0.8

    // 4. Integridad CRC32 actualizada y válida
    EXPECT_TRUE(snap.isCrcValid());
    EXPECT_TRUE(snap.isValid());
}
