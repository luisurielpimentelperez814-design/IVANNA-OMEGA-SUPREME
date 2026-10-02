// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// test_phase4_5_header_rt_wiring.cpp
// Verificación host GTest de los 22 headers de producción consolidados en
// Fase 4 (0 huérfanos en check_header_wiring.py) y las garantías de RT-Safety
// de Fase 5.
// ============================================================================

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include "../SafPcaHRTFBridge.hpp"
#include "../audio_orchestrator.h"
#include "../daemon/core/OmegaDspSnapshot.h"
#include "../evolutionary_adapter.hpp"
#include "../evolutionary_adapter_enhanced.hpp"
#include "../hexagon/hexagon_dsp_integration.hpp"
#include "../hexagon/ivanna_dsp.h"
#include "../hexagon/ivanna_dsp.hpp"
#include "../include/acoustic_cognitive_evolution_engine.hpp"
#include "../include/anti_dolby.h"
#include "../include/audio_bus.h"
#include "../include/hrtf_lut.h"
#include "../include/master_acoustic_orchestrator.hpp"
#include "../include/saf_feedback.h"
#include "../include/saf_socket_update.h"
#include "../include/volterra_h2_symmetric.hpp"
#include "../neuromorphic/ivanna_synthesizer.hpp"
#include "../phase_oracle_refinements.hpp"
#include "../spatial/HybridRenderer.hpp"
#include "../spatial/PerfAuditor.hpp"
#include "../spatial/RoomSimulator.hpp"
#include "../spatial/SpatialRenderer.hpp"

// Definido en evolutionary_adapter_enhanced.hpp como extern
EvolutionaryGenomeMapping g_evo_adapter{};

TEST(Phase45HeaderRtWiring, AllProductionHeadersInstantiateAndOperateCleanly) {
    // 1. Daemon OmegaDspSnapshot CRC32
    omega::OmegaDspSnapshot daemonSnap{};
    daemonSnap.crc32 = daemonSnap.computeCRC32();
    EXPECT_TRUE(daemonSnap.isValid());

    // 2. EvolutionaryAdapter + EvolutionaryGenomeMapping
    ivanna::EvolutionaryAdapter adapter{};
    adapter.init(1337u);
    adapter.update_audio_features(0.8f, 0.1f, 0.5f);
    uint32_t rng = 1337u;
    adapter.evolve_one_generation(rng);
    EXPECT_TRUE(adapter.best_params().valid);
    EXPECT_EQ(adapter.get_generation(), 1u);

    g_evo_adapter.init();
    float fakeGenome[256];
    for (int i = 0; i < 256; ++i) fakeGenome[i] = 0.6f;
    g_evo_adapter.update_best_genome(fakeGenome, 0.95f);
    g_evo_adapter.apply_mapping();
    EXPECT_GT(g_evo_adapter.get_dsp_drive(), 1.0f);
    EXPECT_EQ(g_evo_adapter.get_generation(), 1);

    // 3. KalmanPhasePredictor (phase_oracle_refinements.hpp)
    KalmanPhasePredictor kalman{};
    kalman.init(48000.0f);
    kalman.predict_step();
    kalman.update_step(120.0f, 0.9f);
    EXPECT_GT(kalman.get_refined_period(), 0.0f);
    EXPECT_GE(kalman.get_coherence(), 0.0f);

    // 4. SAF feedback & socket update (saf_feedback.h / saf_socket_update.h)
    updateSAFFeedback(0.5f);
    const float safGain = calculateSAFGain();
    EXPECT_GE(safGain, 0.1f);
    EXPECT_LE(safGain, 2.0f);
    updateSAFFromJson(1.1f, 0.4f, 0.3f, 0.8f);
    EXPECT_FLOAT_EQ(g_saf_state.gain.load(), 1.1f);

    // 5. HRTF LUT (hrtf_lut.h)
    EXPECT_EQ(hrtf_gain_L[0], 32767);
    EXPECT_EQ(hrtf_gain_R[63], 32767);

    // 6. VolterraH2Symmetric canonical forwarder (include/volterra_h2_symmetric.hpp)
    ivanna::dsp::VolterraH2Symmetric volterra(16, 2);
    EXPECT_TRUE(volterra.isEnabled());

    // 7. Synthesizer canonical forwarder (neuromorphic/ivanna_synthesizer.hpp)
    ivanna::acoustic::Synthesizer synth(48000.0f, 50.0f);
    synth.setTargetParameters(0.3f, 0.2f, 0.1f, 0.4f, 0.5f);
    synth.smoothTick(128, 48000.0f);
    float sig[5]{};
    synth.getSignature(sig);
    EXPECT_TRUE(std::isfinite(sig[0]));

    // 8. Spatial headers: RoomSimulator, BinauralRenderer, HybridRenderer
    Ivanna::RoomSimulator roomSim{};
    Ivanna::RoomConfig rc{};
    rc.wetMix = 0.3f;
    rc.stereoSpread = 0.4f;
    roomSim.setConfig(rc);
    roomSim.setParameters(0.6f, 0.35f, 0.42f);
    roomSim.setWetMix(0.3f);
    float inL[16]{1.0f}, inR[16]{1.0f}, outL[16]{}, outR[16]{};
    roomSim.processStereo(inL, inR, outL, outR, 16);
    EXPECT_GT( std::fabs(outL[0]), 0.0f );
    EXPECT_FLOAT_EQ(roomSim.getRoomSize(), 0.6f);

    Ivanna::BinauralRenderer binRenderer{};
    binRenderer.setOrientation(Ivanna::Quaternion{0.9659f, 0.0f, 0.2588f, 0.0f});
    binRenderer.processBinaural(inL, outL, outR, 16, Ivanna::Vector3D{0.35f, 1.0f, 0.1f});
    EXPECT_GT( std::fabs(outL[0]) + std::fabs(outR[0]), 0.0f );

    Ivanna::HybridRenderer hybrid{};
    static_assert(Ivanna::HybridRenderer::kActiveTaps == 128, "HybridRenderer must execute full 128-tap FIR");
    EXPECT_EQ(Ivanna::HybridRenderer::kActiveTaps, 128u);
    hybrid.setEnabled(true);
    hybrid.setBinauralWet(0.7f);
    hybrid.setVirtualAngles(35.0f, 10.0f);
    hybrid.setRoomParameters(0.6f, 0.35f, 0.4f, 0.28f, 0.38f);
    float inStereo[32]{1.0f, 1.0f}, outStereo[32]{};
    hybrid.renderBinaural(inStereo, outStereo, 16);
    EXPECT_GT( std::fabs(outStereo[0]) + std::fabs(outStereo[1]), 0.0f );
    float tele[8]{};
    hybrid.getTelemetry(tele);
    EXPECT_FLOAT_EQ(tele[0], 1.0f);
    EXPECT_FLOAT_EQ(tele[1], 0.7f);
    EXPECT_FLOAT_EQ(tele[2], 35.0f);

    // 9. Hexagon stub & integration headers
    ivanna_dsp_handle_t h = nullptr;
    EXPECT_EQ(ivanna_dsp_open(&h), -1);
    EXPECT_NE(ivanna::hexagon::active_library(), nullptr);
}

TEST(Phase45HeaderRtWiring, PromptMaestroV30PhysicalAcousticsAndCrossfadeVerification) {
    // 1. §5, §6.3, §10: Sala real de referencia (7 × 4 × 4 m = 112 m³) y frecuencia de Schroeder
    const auto refRoom = ivanna::spatial::RoomGeometryConfig::referenceSonyMhcPz1dLayout();
    EXPECT_FLOAT_EQ(refRoom.volumeM3(), 112.0f);
    const float fsDry  = refRoom.schroederFrequencyHz(0.35f);
    const float fsLive = refRoom.schroederFrequencyHz(2.0f);
    EXPECT_NEAR(fsDry,  111.8034f, 0.05f);
    EXPECT_NEAR(fsLive, 267.2612f, 0.05f);

    // 2. §6.2 & §7.7: En sala viva (T60 >= 1.2 s) cola y ER sintéticas están OFF (0.0)
    EXPECT_FLOAT_EQ(
        ivanna::spatial::RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(0.35f, 1.20f),
        0.0f);
    EXPECT_FLOAT_EQ(
        ivanna::spatial::RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(0.35f, 2.00f),
        0.0f);
    EXPECT_FLOAT_EQ(
        ivanna::spatial::RoomGeometryConfig::limitSyntheticReverbWetForRoomT60(0.35f, 0.35f),
        0.35f);

    // MasterAcousticOrchestrator respeta el corte de T60 >= 1.2 s en el snapshot
    ivanna::OmegaDspSnapshot snap{};
    snap.room_rt60_s = 1.45f;
    snap.room_wet    = 0.30f;
    ivanna::experimental::AdaptiveState adaptSt{};
    adaptSt.rir_wet_scale = 1.0f;
    ivanna::MasterAcousticOrchestrator::arbitrateSnapshot(snap, adaptSt);
    EXPECT_FLOAT_EQ(snap.room_wet, 0.0f);

    // 3. §0.2, §4, §7.11: Crossfade de potencia constante (±0.3 dB) y dither -140 dBFS
    EXPECT_FLOAT_EQ(ivanna::supreme::SupremeTransitionEnvelope::kAntiDenormalDither140dBFS, 1.0e-7f);
    for (int step = 0; step <= 100; ++step) {
        const float env = static_cast<float>(step) * 0.01f;
        float gDry = 0.0f, gWet = 0.0f;
        ivanna::supreme::SupremeTransitionEnvelope::constantPowerGains(env, gDry, gWet);
        const float totalPower = gDry * gDry + gWet * gWet;
        const float powerDb = 10.0f * std::log10(std::max(1.0e-12f, totalPower));
        EXPECT_NEAR(powerDb, 0.0f, 0.05f); // Estrictamente dentro de ±0.3 dB
    }

    // 4. §0.4 & §4: HybridRenderer OFF por defecto (sin duplicación de HRTF/cola)
    Ivanna::HybridRenderer defaultHybrid{};
    EXPECT_FALSE(defaultHybrid.isEnabled());

    // 5. §4 & §7.5: ObjectSpatialRenderer soporta bloques de 64/128/256 muestras con suavizado anti-click
    ivanna::spatial::ObjectSpatialRenderer objRenderer{};
    objRenderer.setEstimatedRoomT60(1.50f); // Sala viva -> ER sintéticas OFF
    alignas(16) float objBuf[256]{};
    for (size_t i = 0; i < 256; ++i) objBuf[i] = std::sin(0.1f * static_cast<float>(i));
    const float* inPtrs[4] = {objBuf, objBuf, objBuf, objBuf};
    std::array<ivanna::spatial::DecomposedObject, 4> objs{};
    objs[0].position = {-0.6f, 1.5f, 0.0f};
    objs[1].position = { 0.6f, 1.5f, 0.0f};
    alignas(16) float outL[256]{}, outR[256]{};
    for (size_t blk : {size_t(64), size_t(128), size_t(256)}) {
        objs[0].position.x = -objs[0].position.x; // salto brusco de posición amortiguado con τ=15ms
        objRenderer.renderObjects(inPtrs, objs, outL, outR, blk, 1.0f);
        for (size_t n = 0; n < blk; ++n) {
            EXPECT_TRUE(std::isfinite(outL[n]));
            EXPECT_TRUE(std::isfinite(outR[n]));
        }
    }
}

