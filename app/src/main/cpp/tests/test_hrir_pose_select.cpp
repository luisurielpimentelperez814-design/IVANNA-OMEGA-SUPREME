// Selección del HRIR medido por pose (azimut, elevación) y ley de distancia de la escena (Ruta B).
// Regresión del defecto previo: comparación fabs() solo en azimut, sin envolver y sin elevación.
#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include "spatial/HrirPoseSelect.hpp"

using namespace ivanna::spatial;

namespace {
struct Grid { std::vector<float> az, el; };
// Rejilla tipo IHR1: elevaciones -40..+90 cada 10° (dentro de cada anillo, azimut cada 15°), en el orden en que
// un archivo la escribe (primero la elevación más baja): la primera entrada de cada azimut está a -40°.
Grid ihrLikeGrid() {
    Grid g;
    for (int e = -40; e <= 80; e += 20)
        for (int a = -180; a < 180; a += 15) { g.az.push_back(static_cast<float>(a)); g.el.push_back(static_cast<float>(e)); }
    return g;
}
size_t pick(const Grid& g, float az, float el) {
    return nearestHrirIndex(g.az.size(), [&](size_t i) { return g.az[i]; }, [&](size_t i) { return g.el[i]; }, az, el);
}
// El algoritmo VIEJO, para demostrar que el test discrimina.
size_t legacyAzimuthOnly(const Grid& g, float az) {
    float best = 1e9f; size_t idx = 0;
    for (size_t i = 0; i < g.az.size(); ++i) { const float d = std::fabs(g.az[i] - az); if (d < best) { best = d; idx = i; } }
    return idx;
}
} // namespace

TEST(HrirPoseSelect, AngularDistanceIsASphericalMetricThatWrapsAzimuth) {
    EXPECT_NEAR(angularDistanceDeg(0, 0, 0, 0), 0.f, 1e-3f);
    EXPECT_NEAR(angularDistanceDeg(179, 0, -179, 0), 2.f, 1e-3f);           // envuelve: 2°, no 358°
    EXPECT_NEAR(angularDistanceDeg(0, 0, 90, 0), 90.f, 1e-3f);
    EXPECT_NEAR(angularDistanceDeg(0, 90, 123, 90), 0.f, 1e-2f);            // en el cenit el azimut no importa
    EXPECT_NEAR(angularDistanceDeg(0, 0, 180, 0), 180.f, 1e-3f);
    EXPECT_NEAR(angularDistanceDeg(30, 20, 30, -10), 30.f, 1e-3f);
}

TEST(HrirPoseSelect, PicksTheRequestedElevationNotTheFirstEntryOfThatAzimuth) {
    const Grid g = ihrLikeGrid();
    const size_t i = pick(g, 30.f, 0.f);                                     // plano horizontal
    EXPECT_NEAR(g.az[i], 30.f, 1e-3f);
    EXPECT_LE(std::fabs(g.el[i]), 10.f) << "debe quedar cerca de 0°, no a -40°";
    const size_t legacy = legacyAzimuthOnly(g, 30.f);
    EXPECT_NEAR(g.el[legacy], -40.f, 1e-3f) << "el algoritmo anterior elegía la primera entrada (-40°)";
    const size_t up = pick(g, 30.f, 40.f);
    EXPECT_NEAR(g.el[up], 40.f, 1e-3f);
    EXPECT_NEAR(g.az[up], 30.f, 1e-3f);
}

TEST(HrirPoseSelect, WrapsAroundBehindTheHeadAndAcceptsBothAzimuthConventions) {
    const Grid g = ihrLikeGrid();
    const size_t i = pick(g, 179.f, 0.f);                                    // detrás, justo al otro lado del salto ±180
    EXPECT_LE(angularDistanceDeg(g.az[i], g.el[i], 179.f, 0.f), 10.f);
    // Un dataset 0..360 y un destino negativo apuntan al mismo sitio.
    Grid r; for (int a = 0; a < 360; a += 30) { r.az.push_back(static_cast<float>(a)); r.el.push_back(0.f); }
    const size_t j = pick(r, -90.f, 0.f);
    EXPECT_NEAR(r.az[j], 270.f, 1e-3f);
}

TEST(HrirPoseSelect, IgnoresNonFiniteEntriesAndTargets) {
    Grid g; g.az = {NAN, 10.f, 20.f}; g.el = {0.f, 0.f, NAN};
    EXPECT_EQ(pick(g, 12.f, 0.f), 1u);
    EXPECT_EQ(pick(g, NAN, 0.f), 0u);
    EXPECT_EQ(nearestHrirIndex(0, [](size_t) { return 0.f; }, [](size_t) { return 0.f; }, 0.f, 0.f), 0u);
}

TEST(SceneDistanceGain, InverseLawClampedAndIdentityAtReference) {
    EXPECT_FLOAT_EQ(sceneDistanceGain(1.0f), 1.0f);                          // identidad exacta en la referencia
    EXPECT_NEAR(sceneDistanceGain(2.0f), 0.5f, 1e-6f);                       // -6 dB
    EXPECT_NEAR(sceneDistanceGain(1.5f), 1.f / 1.5f, 1e-6f);
    EXPECT_NEAR(sceneDistanceGain(0.5f), 1.41254f, 1e-5f);                   // tope +3 dB (no 2.0)
    EXPECT_FLOAT_EQ(sceneDistanceGain(NAN), 1.0f);
    EXPECT_FLOAT_EQ(sceneDistanceGain(-3.f), 1.0f);
    EXPECT_FLOAT_EQ(sceneDistanceGain(100.f), 0.5f);
}
