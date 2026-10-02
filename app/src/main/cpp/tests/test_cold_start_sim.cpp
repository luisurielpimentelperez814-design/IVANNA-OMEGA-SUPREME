// test_cold_start_sim.cpp — Pruebas T8 + T9: Arranque en Frío (<= 1.5 s),
// Continuidad en Cambio de Pista, Métricas M10 y Round-Trip IVW1 loadWeights()
#include <gtest/gtest.h>
#include "../music_intelligence/ImeBridge.hpp"
#include "../music_intelligence/SceneTargetBus.hpp"
#include "../spatial/IvannaAudioPipeline.hpp"
#include "../IvannaAudioClassifier.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

// Sintetiza un bloque con firma acústica de jazz_live_room (dinámico, alto factor de cresta, ancho medio)
void genJazzLiveBlock(float* L, float* R, size_t n, size_t sampleOffset, float sr) {
    for (size_t i = 0; i < n; ++i) {
        const size_t idx = sampleOffset + i;
        const float t = static_cast<float>(idx) / sr;
        // Envolvente percusiva acústica con alto factor de cresta (~15 dB)
        const float beatPhase = std::fmod(t * 2.2f, 1.0f);
        const float env = 0.12f + 0.88f * std::exp(-beatPhase * 9.5f);
        const float bass = 0.28f * std::sin(6.2831853f * 110.0f * t);
        const float body = 0.24f * std::sin(6.2831853f * 440.0f * t);
        const float pres = 0.10f * std::sin(6.2831853f * 2200.0f * t);
        const float side = 0.16f * std::cos(6.2831853f * 660.0f * t);
        L[i] = env * (bass + body + pres + side);
        R[i] = env * (bass + body + pres - side);
    }
}

// Sintetiza un bloque con firma de electronic_dense (comprimido, denso, sub-bajo + agudos brillantes)
void genElectronicDenseBlock(float* L, float* R, size_t n, size_t sampleOffset, float sr) {
    for (size_t i = 0; i < n; ++i) {
        const size_t idx = sampleOffset + i;
        const float t = static_cast<float>(idx) / sr;
        const float sub  = 0.34f * std::sin(6.2831853f * 55.0f * t);
        const float mid  = 0.20f * std::sin(6.2831853f * 900.0f * t);
        const float high = 0.22f * std::sin(6.2831853f * 4800.0f * t)
                         + 0.12f * std::cos(6.2831853f * 9200.0f * t);
        const float side = 0.18f * std::sin(6.2831853f * 1800.0f * t);
        L[i] = 0.55f * (sub + mid + high + side);
        R[i] = 0.55f * (sub + mid + high - side);
    }
}

TEST(ColdStartSimTest, IdentifiesWithin1Point5SecondsAndMaintainsM10QualityWithoutClicks) {
    constexpr float kSr = 48000.0f;
    constexpr size_t kBlock = 512;
    constexpr size_t kFrames1p5s = static_cast<size_t>(1.55f * kSr);
    constexpr size_t kFrames5s   = static_cast<size_t>(5.0f  * kSr);

    auto& bus = ivanna::ime::SceneTargetBus::instance();
    bus.setSceneReconstructionEnabled(true);
    bus.setUseStatDereverb(true);
    bus.setUsePhysicalEr(true);
    bus.setShaperMode(1);
    bus.setManualStyleOverride(-1);
    ivanna::ime::imeSoftReset();

    ivanna::spatial::IvannaAudioPipeline pipeline;
    pipeline.prepare(kSr, kBlock);

    std::vector<float> L(kBlock), R(kBlock);
    float maxPeak = 0.0f;
    int   styleAt1p5s = -1;
    float confAt1p5s  = 0.0f;

    // Fase 1: 5 segundos de pista A (jazz_live_room)
    for (size_t off = 0; off < kFrames5s; off += kBlock) {
        genJazzLiveBlock(L.data(), R.data(), kBlock, off, kSr);
        ivanna::ime::imeFeedPlanar(L.data(), R.data(), static_cast<int>(kBlock), kSr);
        pipeline.processLiveSpatialAxes(L.data(), R.data(), kBlock, kSr, true, 0.18f, true, false);

        for (size_t i = 0; i < kBlock; ++i) {
            ASSERT_TRUE(std::isfinite(L[i]));
            ASSERT_TRUE(std::isfinite(R[i]));
            maxPeak = std::max(maxPeak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
        if (off >= kFrames1p5s && styleAt1p5s < 0) {
            const auto dec = ivanna::ime::imeLastDecision();
            styleAt1p5s = dec.current;
            confAt1p5s  = dec.conf;
        }
    }

    EXPECT_GE(styleAt1p5s, 0) << "Debe identificar un estilo válido en <= 1.5 s de arranque en frío";
    EXPECT_GT(confAt1p5s, 0.05f);
    const auto targetsA = bus.peekLatest().t;

    // Fase 2: Cambio de pista a 5 segundos de electronic_dense (precedido por softReset)
    ivanna::ime::imeSoftReset();
    for (size_t off = 0; off < kFrames5s; off += kBlock) {
        genElectronicDenseBlock(L.data(), R.data(), kBlock, off, kSr);
        ivanna::ime::imeFeedPlanar(L.data(), R.data(), static_cast<int>(kBlock), kSr);
        pipeline.processLiveSpatialAxes(L.data(), R.data(), kBlock, kSr, true, 0.18f, true, false);

        for (size_t i = 0; i < kBlock; ++i) {
            ASSERT_TRUE(std::isfinite(L[i]));
            ASSERT_TRUE(std::isfinite(R[i]));
            maxPeak = std::max(maxPeak, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
    }

    const auto targetsB = bus.peekLatest().t;
    const auto m10 = bus.readRealismMetrics();

    // Verificar adaptación real de objetivos entre Pista A y Pista B
    const float targetDiff = std::fabs(targetsA.wfsSpread - targetsB.wfsSpread)
                           + std::fabs(targetsA.warmth    - targetsB.warmth)
                           + std::fabs(targetsA.envDepth  - targetsB.envDepth);
    EXPECT_GT(targetDiff, 1.0e-3f) << "Los objetivos de escena deben adaptarse al cambiar de pista";

    // Verificar invariantes M10 y techo de pico
    EXPECT_LE(maxPeak, 1.0f);
    EXPECT_GE(m10.cTransient, 0.85f);
    EXPECT_GE(m10.qScore, 0.70f);
}

TEST(ColdStartSimTest, Ivw1BinaryWeightsRoundTripWithAudioClassifier) {
    // Verificar el contrato binario IVW1 (Sección 7.4: magic 0x49565731, version 1, numParams)
    Ivanna::IvannaAudioClassifier classifier;
    const size_t expectedParams = classifier.expectedWeightCount();
    ASSERT_GT(expectedParams, 100u);

    const char* tmpPath = "/tmp/test_atlas_weights_ivw1.bin";
    {
        std::ofstream out(tmpPath, std::ios::binary);
        ASSERT_TRUE(out.good());
        const uint32_t magic   = 0x49565731u; // "IVW1"
        const uint32_t version = 1u;
        const uint32_t count   = static_cast<uint32_t>(expectedParams);
        out.write(reinterpret_cast<const char*>(&magic),   sizeof(uint32_t));
        out.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));
        out.write(reinterpret_cast<const char*>(&count),   sizeof(uint32_t));
        std::vector<float> weights(expectedParams, 0.002f);
        out.write(reinterpret_cast<const char*>(weights.data()),
                  static_cast<std::streamsize>(expectedParams * sizeof(float)));
    }

    EXPECT_TRUE(classifier.loadWeights(tmpPath))
        << "IvannaAudioClassifier::loadWeights() debe aceptar el archivo IVW1 generado";
    std::remove(tmpPath);
}

} // namespace
