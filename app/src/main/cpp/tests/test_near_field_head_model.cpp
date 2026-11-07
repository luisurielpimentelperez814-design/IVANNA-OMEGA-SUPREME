// test_near_field_head_model.cpp — propiedades demostrables del modelo de
// cabeza esférica en campo cercano (ORBE). Pura matemática, sin audio.
#include <gtest/gtest.h>
#include <cmath>
#include "../spatial/NearFieldHeadModel.hpp"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.0f;
}  // namespace

TEST(NearFieldHeadModel, ItdIsZeroOnMedianPlane) {
    for (float r : {0.15f, 0.35f, 1.0f, 2.0f, 100.0f}) {
        EXPECT_FLOAT_EQ(ivanna::nearfield::itdSeconds(0.0f, r), 0.0f) << "r=" << r;
    }
}

TEST(NearFieldHeadModel, ItdIsAntisymmetricInAzimuth) {
    for (float deg : {15.0f, 30.0f, 60.0f, 90.0f, 120.0f}) {
        const float a = ivanna::nearfield::itdSeconds(deg * kDeg, 0.35f);
        const float b = ivanna::nearfield::itdSeconds(-deg * kDeg, 0.35f);
        EXPECT_FLOAT_EQ(a, -b) << deg;
        EXPECT_GT(a, 0.0f);
    }
}

TEST(NearFieldHeadModel, ItdMatchesWoodworthInFarField) {
    // r = 100 m debe coincidir con Woodworth dentro del 1 %.
    for (float deg : {15.0f, 30.0f, 60.0f, 90.0f, 120.0f, 150.0f}) {
        const float th = deg * kDeg;
        const float model = ivanna::nearfield::itdSeconds(th, 100.0f);
        const float wood = ivanna::nearfield::itdWoodworthFarField(th);
        EXPECT_NEAR(model, wood, 0.01 * wood) << deg;
    }
    // Woodworth a 30°: (a/c)(θ+sinθ) ≈ 0.261 ms (mismo valor del crossfeed).
    EXPECT_NEAR(ivanna::nearfield::itdWoodworthFarField(30.0f * kDeg), 0.000261f, 5e-6f);
    // Woodworth a 90°: (a/c)(π/2+1) ≈ 0.656 ms.
    EXPECT_NEAR(ivanna::nearfield::itdWoodworthFarField(90.0f * kDeg), 0.000656f, 5e-6f);
}

TEST(NearFieldHeadModel, ItdGrowsAsSourceApproaches) {
    // Física de campo cercano: a igual azimut, la fuente cercana da más ITD.
    const float th = 90.0f * kDeg;
    const float near_ = ivanna::nearfield::itdSeconds(th, 0.15f);
    const float mid = ivanna::nearfield::itdSeconds(th, 0.35f);
    const float far_ = ivanna::nearfield::itdSeconds(th, 2.0f);
    EXPECT_GT(near_, mid);
    EXPECT_GT(mid, far_);
    EXPECT_GT(near_, 0.00070f);  // ~0.73 ms medido del modelo geométrico
    EXPECT_LT(near_, 0.00080f);
    EXPECT_NEAR(far_, ivanna::nearfield::itdWoodworthFarField(th), 0.02f * 0.000656f);
}

TEST(NearFieldHeadModel, IldIsZeroOnMedianPlaneAndAntisymmetric) {
    for (float r : {0.15f, 1.0f}) {
        for (float f : {100.0f, 1000.0f, 4000.0f}) {
            EXPECT_FLOAT_EQ(ivanna::nearfield::ildDb(0.0f, r, f), 0.0f);
        }
    }
    const float a = ivanna::nearfield::ildDb(60.0f * kDeg, 0.35f, 1000.0f);
    const float b = ivanna::nearfield::ildDb(-60.0f * kDeg, 0.35f, 1000.0f);
    EXPECT_FLOAT_EQ(a, -b);
    EXPECT_GT(a, 0.0f);
}

TEST(NearFieldHeadModel, IldVanishesAtLowFrequencyInFarField) {
    // En campo lejano y graves, el ILD es casi nulo (por eso el estéreo de
    // altavoces suena "dentro de la cabeza" con audífonos).
    EXPECT_LT(std::fabs(ivanna::nearfield::ildDb(90.0f * kDeg, 2.0f, 100.0f)), 2.0f);
    EXPECT_LT(std::fabs(ivanna::nearfield::ildDb(90.0f * kDeg, 10.0f, 100.0f)), 1.0f);
}

TEST(NearFieldHeadModel, IldIsLargeForCloseSources) {
    // 0.15 m / 90° / 1.5 kHz: divergencia esférica + sombra ≈ 16–22 dB.
    const float v = ivanna::nearfield::ildDb(90.0f * kDeg, 0.15f, 1500.0f);
    EXPECT_GT(v, 16.0f);
    EXPECT_LT(v, 22.0f);
    // Y crece con la frecuencia a igual geometría.
    EXPECT_LT(ivanna::nearfield::ildDb(90.0f * kDeg, 0.15f, 100.0f),
              ivanna::nearfield::ildDb(90.0f * kDeg, 0.15f, 4000.0f));
}

TEST(NearFieldHeadModel, ProximityShelfIsBoundedAndLowFrequencyOnly) {
    EXPECT_FLOAT_EQ(ivanna::nearfield::proximityShelfDb(1.0f, 100.0f), 0.0f);
    EXPECT_FLOAT_EQ(ivanna::nearfield::proximityShelfDb(2.0f, 100.0f), 0.0f);
    const float nearLow = ivanna::nearfield::proximityShelfDb(0.15f, 100.0f);
    EXPECT_GT(nearLow, 4.0f);
    EXPECT_LE(nearLow, 6.0f);
    EXPECT_LT(ivanna::nearfield::proximityShelfDb(0.15f, 4000.0f), 1.0f);
    // A 0.15 m y 0.35 m el realce ya toca el techo de +6 dB (× shape 0.9 a
    // 100 Hz = 5.4 dB en ambos): la monotonía se verifica por debajo del
    // techo, entre 0.7 m y 1.0 m.
    EXPECT_FLOAT_EQ(nearLow, ivanna::nearfield::proximityShelfDb(0.35f, 100.0f));
    EXPECT_GT(ivanna::nearfield::proximityShelfDb(0.7f, 100.0f),
              ivanna::nearfield::proximityShelfDb(1.0f, 100.0f));
}

TEST(NearFieldHeadModel, NonFiniteInputsAreSafe) {
    EXPECT_FLOAT_EQ(ivanna::nearfield::itdSeconds(NAN, 0.35f), 0.0f);
    EXPECT_FLOAT_EQ(ivanna::nearfield::ildDb(0.5f, NAN, 1000.0f), 0.0f);
    EXPECT_FLOAT_EQ(ivanna::nearfield::proximityShelfDb(NAN, 100.0f), 0.0f);
    EXPECT_TRUE(std::isfinite(ivanna::nearfield::itdSeconds(3.0f, 0.05f)));
    EXPECT_TRUE(std::isfinite(ivanna::nearfield::ildDb(3.0f, 0.05f, 20000.0f)));
}
