// ============================================================================
// test_binaural_crossfade.cpp — el BinauralRenderer no debe partir la forma de onda al cambiar de filtro.
//
// Un giro de cabeza (o una fuente que se mueve) cambia los coeficientes HRTF. Cambiarlos de golpe deja una
// discontinuidad (clic). Se mide el salto máximo entre muestras consecutivas alrededor del cambio contra el de
// régimen estacionario, y se comprueba que el cambio ES grande entre los dos filtros (el test sería vacío si no).
// Datos sintéticos de prueba (pulsos con ITD y ganancia distintos por azimut), nunca enlazados al producto.
// ============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "spatial/HRTFDatabase.h"
#include "spatial/SpatialRenderer.hpp"
#include "spatial/HybridRenderer.hpp"

using namespace Ivanna;

namespace {
constexpr double kPi = 3.14159265358979323846;

void buildTable() {
    hrtfDatabaseUnloadForTesting();
    std::vector<float> az, el, L, R;
    const size_t n = 128;
    for (double e = -60; e <= 60; e += 15)
        for (double a = 0; a < 360; a += 15) {
            az.push_back(float(a)); el.push_back(float(e));
            const double s = std::sin(a * kPi / 180.0) * std::cos(e * kPi / 180.0);
            std::vector<float> l(n), r(n);
            for (size_t i = 0; i < n; ++i) {
                const double xl = (double(i) - (40.0 - 16.0 * s)) / 0.9, xr = (double(i) - (40.0 + 16.0 * s)) / 0.9;
                l[i] = float((1.0 + 0.5 * s) * std::exp(-0.5 * xl * xl));
                r[i] = float((1.0 - 0.5 * s) * std::exp(-0.5 * xr * xr));
            }
            L.insert(L.end(), l.begin(), l.end());
            R.insert(R.end(), r.begin(), r.end());
        }
    HrtfDbMeasured m;
    m.azDeg = az.data(); m.elDeg = el.data(); m.left = L.data(); m.right = R.data();
    m.numPositions = az.size(); m.irLen = n; m.sampleRateHz = 48000;
    ASSERT_EQ(hrtfDatabaseBuild(m, true), HrtfDbResult::Ok);
}

Quaternion yaw(double deg) {
    const double h = 0.5 * deg * kPi / 180.0;
    return { float(std::cos(h)), 0.0f, 0.0f, float(std::sin(h)) };
}

struct Rendered { std::vector<float> L, R; };

// Renderiza `blocks` bloques de `blk` muestras de un seno de 1 kHz; en el bloque `switchAt` gira la cabeza `deg`.
Rendered render(double deg, int switchAt, int blocks, int blk) {
    BinauralRenderer r;
    r.setOrientation(yaw(0));
    Rendered out;
    std::vector<float> in(blk), l(blk), rr(blk);
    long t = 0;
    for (int b = 0; b < blocks; ++b) {
        if (b == switchAt) r.setOrientation(yaw(deg));
        for (int i = 0; i < blk; ++i, ++t) in[i] = 0.5f * float(std::sin(2.0 * kPi * 1000.0 * double(t) / 48000.0));
        r.processBinaural(in.data(), l.data(), rr.data(), size_t(blk), Vector3D{0.0f, 1.0f, 0.0f});
        out.L.insert(out.L.end(), l.begin(), l.end());
        out.R.insert(out.R.end(), rr.begin(), rr.end());
    }
    return out;
}

double maxStep(const std::vector<float>& x, size_t from, size_t to) {
    double m = 0;
    for (size_t i = std::max<size_t>(from, 1); i < to && i < x.size(); ++i) m = std::max(m, double(std::fabs(x[i] - x[i - 1])));
    return m;
}
}  // namespace

TEST(BinauralCrossfade, HeadTurnDoesNotCreateAClick) {
    buildTable();
    const int blk = 64, sw = 60, blocks = 120;
    const Rendered steady = render(0.0, 1000, blocks, blk);                 // sin giro
    const Rendered turned = render(40.0, sw, blocks, blk);                  // giro de 40° en el bloque `sw`
    const size_t from = size_t(sw) * blk, to = from + 600;             // ventana del fundido y un poco más
    const double stepSteady = std::max(maxStep(steady.L, 2000, 3000), maxStep(steady.R, 2000, 3000));
    const double stepTurn   = std::max(maxStep(turned.L, from, to), maxStep(turned.R, from, to));

    // El cambio de filtro ES grande: la diferencia entre ambos regímenes estacionarios supera con holgura el paso normal.
    const Rendered later = render(40.0, 0, blocks, blk);
    double gap = 0; for (size_t i = 3000; i < 3200; ++i) gap = std::max(gap, double(std::fabs(later.L[i] - steady.L[i])));
    EXPECT_GT(gap, 0.15) << "el test no discrimina si los dos filtros suenan casi igual";

    EXPECT_LT(stepTurn, 1.6 * stepSteady) << "salto " << stepTurn << " vs régimen estacionario " << stepSteady;
    hrtfDatabaseUnloadForTesting();
}

TEST(BinauralCrossfade, ConvergesToTheNewFilterAndStaysFinite) {
    buildTable();
    const int blk = 64, blocks = 140;
    const Rendered turned = render(40.0, 20, blocks, blk);
    const Rendered direct = render(40.0, 0, blocks, blk);                   // ya girada desde el principio
    for (size_t i = 5000; i < 5200; ++i) {
        ASSERT_TRUE(std::isfinite(turned.L[i]) && std::isfinite(turned.R[i]));
        EXPECT_NEAR(turned.L[i], direct.L[i], 1e-3f) << i;             // tras el fundido es el filtro nuevo exacto
        EXPECT_NEAR(turned.R[i], direct.R[i], 1e-3f) << i;
    }
    hrtfDatabaseUnloadForTesting();
}

TEST(BinauralCrossfade, ContinuousSweepStaysSmooth) {
    buildTable();
    BinauralRenderer r;
    std::vector<float> in(48), l(48), rr(48), allL;
    long t = 0;
    for (int b = 0; b < 400; ++b) {
        r.setOrientation(yaw(0.9 * b));                                // giro continuo: un filtro nuevo en cada bloque
        for (int i = 0; i < 48; ++i, ++t) in[i] = 0.5f * float(std::sin(2.0 * kPi * 500.0 * double(t) / 48000.0));
        r.processBinaural(in.data(), l.data(), rr.data(), 48, Vector3D{0.0f, 1.0f, 0.0f});
        allL.insert(allL.end(), l.begin(), l.end());
    }
    // 500 Hz, amplitud <= ~1.5 tras el filtro: el paso máximo natural es ~1.5*2π*500/48000 ≈ 0.1; un clic lo supera de lejos.
    EXPECT_LT(maxStep(allL, 1000, allL.size()), 0.2);
    hrtfDatabaseUnloadForTesting();
}

// HybridRenderer: mover los ángulos virtuales (o un objeto) cambia 512 coeficientes; debe fundirse, no partir la onda.
namespace {
Rendered hybrid(float az1, int switchAt, int blocks, int blk) {
    HybridRenderer h;
    h.setBinauralWet(1.0f);
    h.setRoomParameters(0.3f, 0.5f, 0.5f, 0.0f);
    h.setVirtualAngles(10.0f, 0.0f);
    h.setEnabled(true);
    Rendered out;
    std::vector<float> l(blk), r(blk);
    long t = 0;
    for (int b = 0; b < blocks; ++b) {
        if (b == switchAt) h.setVirtualAngles(az1, 0.0f);
        for (int i = 0; i < blk; ++i, ++t) { l[i] = 0.5f * float(std::sin(2.0 * kPi * 1000.0 * double(t) / 48000.0)); r[i] = 0.0f; }   // solo canal izquierdo: el fundido se nota en ambos oídos
        h.renderPlanar(l.data(), r.data(), size_t(blk));
        out.L.insert(out.L.end(), l.begin(), l.end());
        out.R.insert(out.R.end(), r.begin(), r.end());
    }
    return out;
}
}  // namespace

TEST(HybridCrossfade, VirtualAngleChangeDoesNotCreateAClick) {
    buildTable();
    const int blk = 64, sw = 80, blocks = 160;
    const Rendered steady = hybrid(10.0f, 1000, blocks, blk);
    const Rendered moved  = hybrid(-70.0f, sw, blocks, blk);
    const Rendered later  = hybrid(-70.0f, 0, blocks, blk);
    double gap = 0; for (size_t i = 8000; i < 8200; ++i) gap = std::max(gap, double(std::fabs(later.L[i] - steady.L[i])));
    EXPECT_GT(gap, 0.05) << "el test no discrimina si los dos filtros suenan casi igual";
    const size_t from = size_t(sw) * blk, to = from + 600;
    const double stepSteady = std::max(maxStep(steady.L, 6000, 7000), maxStep(steady.R, 6000, 7000));
    const double stepMove   = std::max(maxStep(moved.L, from, to), maxStep(moved.R, from, to));
    EXPECT_LT(stepMove, 1.6 * stepSteady) << "salto " << stepMove << " vs " << stepSteady;
    for (size_t i = 9000; i < 9200; ++i) {
        EXPECT_NEAR(moved.L[i], later.L[i], 2e-3f);   // tras el fundido es el filtro nuevo
        EXPECT_NEAR(moved.R[i], later.R[i], 2e-3f);
    }
    hrtfDatabaseUnloadForTesting();
}
