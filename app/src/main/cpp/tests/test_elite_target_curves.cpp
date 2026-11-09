// test_elite_target_curves.cpp — Curvas objetivo ÉLITE v2.5.369.
// Harman AE/OE 2018 (Olive/Welti/Khonsaripour, AES 2018), variante
// élite-lean, campo difuso H&M y altavoces en sala (Olive/Welti 2013).
// Valores del informe research_notes/elite-electroacoustic-signatures-
// 20261108-2208/report.md (§10–§12). El punto Harman a 20 kHz (−17,1 dB)
// es un artefacto del replicador y NO forma parte de la curva.
#include <cmath>

#include <gtest/gtest.h>

#include "../spatial/EliteTargetCurves.hpp"

using namespace ivanna::spatial;

TEST(EliteTargetCurves, Harman2018Anchors) {
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 20.0f), 3.5f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 50.0f), 3.3f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 100.0f), 1.1f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 200.0f), -2.0f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 500.0f), -0.8f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 2000.0f), 5.3f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 3150.0f), 8.6f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 4000.0f), 8.4f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 6300.0f), 4.8f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 8000.0f), 2.5f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 10000.0f), -0.6f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 16000.0f), -8.3f);
}

TEST(EliteTargetCurves, TwentyKilohertzArtifactExcluded) {
    // El artefacto de 20 kHz (−17,1 dB) NO se usa: por encima de 16 kHz
    // la curva se clampa al valor de 16 kHz (−8,3 dB).
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 20000.0f),
                    -8.3f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 100000.0f),
                    -8.3f);
    EXPECT_FLOAT_EQ(eliteTargetDbAt(EliteTargetKind::Harman2018, 10.0f), 3.5f);
}

TEST(EliteTargetCurves, InterpolationIsLogLinear) {
    // Punto medio logarítmico entre 1000 Hz (0 dB) y 2000 Hz (+5,3 dB):
    // a 1414 Hz debe valer +2,65 dB.
    EXPECT_NEAR(eliteTargetDbAt(EliteTargetKind::Harman2018, 1414.2135f),
                2.65f, 0.01f);
    // Entre 100 Hz (+1,1) y 200 Hz (−2,0): a 141,4 Hz ≈ −0,45 dB.
    EXPECT_NEAR(eliteTargetDbAt(EliteTargetKind::Harman2018, 141.4213f),
                -0.45f, 0.01f);
}

TEST(EliteTargetCurves, EliteLeanIsHarmanMinus3DbBass) {
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::HarmanEliteLean, 20.0f), 0.5f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::HarmanEliteLean, 50.0f), 0.3f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::HarmanEliteLean, 100.0f), -0.9f);
    // Resto idéntico a Harman (incluido 10 kHz: −0,6 dB).
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::HarmanEliteLean, 3150.0f), 8.6f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::HarmanEliteLean, 10000.0f), -0.6f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::HarmanEliteLean, 16000.0f), -8.3f);
}

TEST(EliteTargetCurves, DiffuseFieldHM) {
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::DiffuseFieldHM, 100.0f), -4.0f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::DiffuseFieldHM, 3150.0f), 11.5f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::DiffuseFieldHM, 10000.0f), 3.0f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::DiffuseFieldHM, 16000.0f), -4.0f);
}

TEST(EliteTargetCurves, RoomSpeakersShelves) {
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::RoomSpeakers, 20.0f), 6.6f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::RoomSpeakers, 105.0f), 6.6f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::RoomSpeakers, 1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::RoomSpeakers, 2500.0f), 0.0f);
    EXPECT_FLOAT_EQ(
        eliteTargetDbAt(EliteTargetKind::RoomSpeakers, 16000.0f), -2.4f);
}

TEST(EliteTargetCurves, Build24Bands) {
    float centers[24];
    eliteErbCenters24(48000.0f, centers);
    ASSERT_GT(centers[0], 60.0f);
    ASSERT_LT(centers[0], 100.0f);
    for (int b = 1; b < 24; ++b) ASSERT_GT(centers[b], centers[b - 1]);
    ASSERT_LT(centers[23], 16000.0f);

    float curve[24];
    buildEliteTarget24(EliteTargetKind::Harman2018, centers, curve);
    for (int b = 0; b < 24; ++b) {
        EXPECT_TRUE(std::isfinite(curve[b]));
        EXPECT_GE(curve[b], -8.3f);
        EXPECT_LE(curve[b], 8.6f);
    }
    // La banda más cercana a 1 kHz vale ~0 dB; la más cercana a 3,15 kHz
    // vale ~+8,6 dB.
    int near1k = 0, near3150 = 0;
    float d1 = 1e9f, d3 = 1e9f;
    for (int b = 0; b < 24; ++b) {
        if (std::fabs(std::log(centers[b] / 1000.0f)) < d1) {
            d1 = std::fabs(std::log(centers[b] / 1000.0f));
            near1k = b;
        }
        if (std::fabs(std::log(centers[b] / 3150.0f)) < d3) {
            d3 = std::fabs(std::log(centers[b] / 3150.0f));
            near3150 = b;
        }
    }
    EXPECT_NEAR(curve[near1k], 0.0f, 0.5f);
    EXPECT_NEAR(curve[near3150], 8.6f, 0.5f);
}

TEST(EliteTargetCurves, BassTrim) {
    float centers[24];
    eliteErbCenters24(48000.0f, centers);
    float curve[24];
    buildEliteTarget24(EliteTargetKind::Harman2018, centers, curve);
    float base[24];
    for (int b = 0; b < 24; ++b) base[b] = curve[b];

    // +3 dB: sube las bandas graves. La banda 0 (centro ~82 Hz) está en
    // la zona de taper (80–250 Hz): recibe 3 dB × w, con
    // w = (ln250 − ln f)/(ln250 − ln80) ≈ 0.978, no los 3 dB completos
    // (esos solo son para f ≤ 80 Hz). 1 kHz no se toca.
    const float w0 = (std::log(250.0f) - std::log(centers[0])) /
                     (std::log(250.0f) - std::log(80.0f));
    applyBassTrimDb(curve, centers, 3.0f);
    EXPECT_NEAR(curve[0], base[0] + 3.0f * w0, 0.01f);
    int near1k = 0;
    float d1 = 1e9f;
    for (int b = 0; b < 24; ++b) {
        if (std::fabs(std::log(centers[b] / 1000.0f)) < d1) {
            d1 = std::fabs(std::log(centers[b] / 1000.0f));
            near1k = b;
        }
    }
    EXPECT_FLOAT_EQ(curve[near1k], base[near1k]);

    // −3 dB y recorte fuera de rango (±3 dB).
    buildEliteTarget24(EliteTargetKind::Harman2018, centers, curve);
    applyBassTrimDb(curve, centers, -3.0f);
    EXPECT_NEAR(curve[0], base[0] - 3.0f * w0, 0.01f);
    buildEliteTarget24(EliteTargetKind::Harman2018, centers, curve);
    applyBassTrimDb(curve, centers, 99.0f);
    EXPECT_NEAR(curve[0], base[0] + 3.0f * w0, 0.01f);
    buildEliteTarget24(EliteTargetKind::Harman2018, centers, curve);
    applyBassTrimDb(curve, centers, std::nanf(""));
    EXPECT_FLOAT_EQ(curve[0], base[0]);

    // eliteTarget24ForSampleRate: misma curva que build con centros.
    float curve2[24];
    eliteTarget24ForSampleRate(EliteTargetKind::Harman2018, 48000.0f, curve2);
    buildEliteTarget24(EliteTargetKind::Harman2018, centers, curve);
    for (int b = 0; b < 24; ++b) EXPECT_FLOAT_EQ(curve2[b], curve[b]);
}
