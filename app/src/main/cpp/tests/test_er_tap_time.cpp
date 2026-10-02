// test_er_tap_time.cpp — Prueba T6: Reflexiones Tempranas Físicas por Fuentes Imagen (M7)
#include <gtest/gtest.h>
#include "../spatial/RoomProjectionEngine.hpp"
#include <cmath>

namespace {

using namespace ivanna::spatial;

TEST(ErTapTimeTest, LateralWallTapMatchesImageSourceGeometryAndBinauralAsymmetry) {
    constexpr float kSr = 48000.0f;
    PhysicalEarlyReflections er;
    er.prepare(kSr);

    // Sala 6 x 8 x 3 m, RT60 = 0.45 s
    er.setRoom(6.0f, 8.0f, 3.0f, 0.45f);

    const float Ds = std::clamp(0.28f * 8.0f, 1.4f, 4.0f); // 2.24 m
    const float dWall = std::hypot(6.0f, Ds);
    const int expectedTap = static_cast<int>(std::lround(((dWall - Ds) / 343.0f) * kSr));

    // k = 0 es pared izquierda (theta < 0)
    EXPECT_NEAR(er.tapSamples(0), expectedTap, 2)
        << "El tap de pared lateral debe caer dentro de ±2 muestras de la ley geométrica";
    EXPECT_GT(er.gainL(0), er.gainR(0))
        << "Para pared izquierda (theta < 0), gL debe ser mayor que gR";

    // k = 1 es pared derecha (theta > 0)
    EXPECT_NEAR(er.tapSamples(1), expectedTap, 2);
    EXPECT_GT(er.gainR(1), er.gainL(1))
        << "Para pared derecha (theta > 0), gR debe ser mayor que gL";
}

} // namespace
