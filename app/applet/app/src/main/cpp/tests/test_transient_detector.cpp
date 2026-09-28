// ============================================================================
//  test_transient_detector.cpp — barrera real del detector de transientes
//  (spatial/TransientDetector.hpp), sub-módulo de la Fase 1 del encargo
//  HOA->Binaural->Upmixing.
//
//  Qué se verifica de verdad (nada de smoke tests):
//   1. Silencio digital NO dispara (el ratio contra el piso no es señal).
//   2. Un tono sostenido NO dispara (sube la envolvente rápida y la lenta).
//   3. Un golpe percusivo SÍ dispara.
//   4. NaN/Inf de una etapa previa no envenenan las envolventes.
// ============================================================================

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "../spatial/TransientDetector.hpp"

namespace {

constexpr float kSr = 48000.0f;

std::vector<float> sine(float freq, float amp, std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = amp * std::sin(2.0f * 3.14159265358979f * freq *
                              static_cast<float>(i) / kSr);
    }
    return v;
}

}  // namespace

TEST(TransientDetector, SilenceNeverTriggers) {
    ivanna::TransientDetector d;
    d.prepare(kSr);
    std::vector<float> silence(2048, 0.0f);
    for (int b = 0; b < 8; ++b) {
        EXPECT_FALSE(d.processBlock(silence.data(), silence.size()));
    }
    EXPECT_TRUE(std::isfinite(d.envelopeFast()));
    EXPECT_TRUE(std::isfinite(d.envelopeSlow()));
}

TEST(TransientDetector, SustainedToneDoesNotTrigger) {
    ivanna::TransientDetector d;
    d.prepare(kSr);
    const auto tone = sine(440.0f, 0.5f, 4096);
    // El arranque del tono desde el silencio ES un transiente legítimo
    // (ataque) y la envolvente lenta tarda ~150 ms en asentarse (7200
    // muestras @48k = 2 bloques largos). Lo que NO debe pasar es que el tono
    // ya establecido siga disparando bloque tras bloque.
    for (int b = 0; b < 4; ++b) d.processBlock(tone.data(), tone.size());
    int hits = 0;
    for (int b = 0; b < 10; ++b) {
        if (d.processBlock(tone.data(), tone.size())) ++hits;
    }
    EXPECT_EQ(hits, 0) << "tono sostenido marcado como transiente " << hits << " veces";
}

TEST(TransientDetector, PercussiveHitTriggers) {
    ivanna::TransientDetector d;
    d.prepare(kSr);
    // Fondo bajo sostenido para asentar la envolvente lenta.
    const auto bed = sine(220.0f, 0.02f, 4096);
    for (int b = 0; b < 6; ++b) d.processBlock(bed.data(), bed.size());

    // Golpe: 0.9 de pico con decaimiento rápido sobre el mismo fondo.
    std::vector<float> hit = bed;
    for (std::size_t i = 0; i < 256; ++i) {
        hit[i] += 0.9f * std::exp(-static_cast<float>(i) / 40.0f);
    }
    EXPECT_TRUE(d.processBlock(hit.data(), hit.size()));
    EXPECT_GT(d.lastRatio(), d.getThreshold());
}

TEST(TransientDetector, NonFiniteSamplesDoNotPoisonEnvelopes) {
    ivanna::TransientDetector d;
    d.prepare(kSr);
    auto tone = sine(440.0f, 0.3f, 1024);
    tone[10]  = std::numeric_limits<float>::quiet_NaN();
    tone[11]  = std::numeric_limits<float>::infinity();
    tone[12]  = -std::numeric_limits<float>::infinity();
    d.processBlock(tone.data(), tone.size());
    EXPECT_TRUE(std::isfinite(d.envelopeFast()));
    EXPECT_TRUE(std::isfinite(d.envelopeSlow()));
    EXPECT_TRUE(std::isfinite(d.lastRatio()));
}

TEST(TransientDetector, PrepareRejectsInvalidSampleRate) {
    ivanna::TransientDetector d;
    d.prepare(std::numeric_limits<float>::quiet_NaN());
    const auto tone = sine(440.0f, 0.3f, 512);
    d.processBlock(tone.data(), tone.size());
    EXPECT_TRUE(std::isfinite(d.envelopeSlow()));
    d.prepare(-48000.0f);
    d.processBlock(tone.data(), tone.size());
    EXPECT_TRUE(std::isfinite(d.envelopeSlow()));
}
