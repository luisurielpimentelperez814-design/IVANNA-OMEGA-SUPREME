// test_4d_control_surface.cpp — Puerta host de la superficie global de la Cadena 4D.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Ejercita el MISMO camino que usa el dispositivo (ensureInit / setEnabled / feedPlanar /
// applyOutputPlanar / publishAnticipatedScene / status) sobre la cadena y el extractor
// REALES: 1) apagado = identidad estricta; 2) apertura anticipada con señal periódica
// (golpe 1 de cada 50 bloques = 120 BPM); 3) techo háptico; 4) dosis finita;
// 5) publicación real en el SceneTargetBus.
#include <gtest/gtest.h>

#include "music_intelligence/FourDControlSurface.hpp"
#include "music_intelligence/SceneTargetBus.hpp"

#include <cmath>
#include <vector>

using namespace ivanna;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int   kSr = 48000;
constexpr int   kBlock = 480;      // 10 ms
constexpr int   kBeatBlocks = 50;  // 500 ms @ 120 BPM

// Señal periódica REAL: un golpe (click + cuerpo 60 Hz) en el bloque 0 de cada compás y
// un colchón tenue en los 49 bloques restantes. La rejilla del golpe NO se repite cada
// bloque (si cada bloque fuese idéntico, transientRate quedaría plano y no habría onset).
void fillBeatBlock(std::vector<float>& l, std::vector<float>& r, int phaseBlock) {
    for (int i = 0; i < kBlock; ++i) {
        const float tt = static_cast<float>(i) / kSr;
        float s;
        if (phaseBlock == 0) {
            const float click = (tt < 0.002f) ? std::exp(-tt * 1500.f) : 0.f;
            const float body  = std::sin(2.f * kPi * 60.f * tt) * std::exp(-tt * 18.f);
            s = 0.9f * (0.6f * body + 0.4f * click);
        } else {
            s = 0.015f * std::sin(2.f * kPi * 300.f * tt);
        }
        l[i] = r[i] = s;
    }
}
} // namespace

TEST(FourDControlSurface, InitializeIsIdempotent) {
    fourd::ensureInit(48000.f, kBlock);
    EXPECT_TRUE(fourd::isInitialized());
    fourd::ensureInit(48000.f, kBlock);
    EXPECT_TRUE(fourd::isInitialized());
}

TEST(FourDControlSurface, DisabledIsBitExact) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(false);
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) { l[i] = std::sin(0.013f * i) * 0.5f; r[i] = std::cos(0.021f * i) * 0.4f; }
    const std::vector<float> l0 = l, r0 = r;
    fourd::applyOutputPlanar(l.data(), r.data(), kBlock);
    for (int i = 0; i < kBlock; ++i) { EXPECT_FLOAT_EQ(l[i], l0[i]); EXPECT_FLOAT_EQ(r[i], r0[i]); }
    EXPECT_FALSE(fourd::status().enabled);
}

TEST(FourDControlSurface, PredictsOnsetOn120Bpm) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);

    std::vector<float> l(kBlock), r(kBlock);
    bool  armedEver = false;
    float lastPeriod = 0.f;

    for (int b = 0; b < kBeatBlocks * 12; ++b) {
        fillBeatBlock(l, r, b % kBeatBlocks);
        fourd::feedPlanar(l.data(), r.data(), kBlock);
        const auto st = fourd::status();
        if (st.armed && st.periodMs > 0.f) { armedEver = true; lastPeriod = st.periodMs; }
    }
    EXPECT_TRUE(fourd::status().enabled);
    EXPECT_TRUE(armedEver) << "el conductor no se armó con un patrón estable de 120 BPM";
    EXPECT_GT(lastPeriod, 350.f);
    EXPECT_LT(lastPeriod, 650.f);
}

TEST(FourDControlSurface, HapticsRespectHardCeiling) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setHapticsEnabled(true);
    fourd::setHapticScale(0.85f);
    std::vector<float> l(kBlock), r(kBlock);
    for (int b = 0; b < kBeatBlocks * 6; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const float tt = static_cast<float>(i) / kSr;
            const float click = (tt < 0.002f) ? std::exp(-tt * 1500.f) : 0.f;
            const float body  = std::sin(2.f * kPi * 60.f * tt) * std::exp(-tt * 18.f);
            l[i] = r[i] = 0.95f * (0.6f * body + 0.4f * click);
        }
        fourd::feedPlanar(l.data(), r.data(), kBlock);
        const auto st = fourd::status();
        EXPECT_LE(st.hapticAmplitude, 255);
        EXPECT_TRUE(std::isfinite(st.hapticTotal));
        EXPECT_LE(st.hapticTotal, 4.0f);
    }
}

TEST(FourDControlSurface, DosimeterAccumulates) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);
    fourd::setDosimeterCalibration(100.0f);
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) { const float v = 0.5f * std::sin(2.f * kPi * 1000.f * i / kSr); l[i] = r[i] = v; }
    for (int b = 0; b < 1000; ++b) fourd::feedPlanar(l.data(), r.data(), kBlock);
    const auto st = fourd::status();
    EXPECT_GT(st.exposureDose, 0.f);
    EXPECT_TRUE(std::isfinite(st.exposureLaeq));
    EXPECT_GT(st.exposureLaeq, 0.f);
}

TEST(FourDControlSurface, PublishesAnticipatedScene) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) { const float v = 0.3f * std::sin(2.f * kPi * 220.f * i / kSr); l[i] = v; r[i] = -v; }
    fourd::feedPlanar(l.data(), r.data(), kBlock);
    const uint32_t before = fourd::publishedScenes();
    fourd::publishAnticipatedScene();
    EXPECT_GT(fourd::publishedScenes(), before);
    EXPECT_GE(ime::SceneTargetBus::instance().peekLatest().seq, 1u);
}
